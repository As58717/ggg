#include "PanoramaCaptureColorConversion.h"
#include "Math/Float16Color.h"

namespace PanoramaCapture
{
namespace Color
{
namespace
{
    FORCEINLINE uint8 ClampToByte(float Value)
    {
        return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value), 0, 255));
    }

    FORCEINLINE uint16 ClampToTenBit(float Value)
    {
        return static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(Value), 0, 1023));
    }

    void ExtractGammaAdjustedRGB(const FFloat16Color& Pixel, EPanoramaGamma GammaMode, float& OutR, float& OutG, float& OutB, float& OutA)
    {
        const FLinearColor LinearColor(Pixel.R.GetFloat(), Pixel.G.GetFloat(), Pixel.B.GetFloat(), Pixel.A.GetFloat());
        OutA = FMath::Clamp(LinearColor.A, 0.0f, 1.0f);

        if (GammaMode == EPanoramaGamma::SRGB)
        {
            const FColor SRGBColor = LinearColor.GetClamped().ToFColorSRGB();
            OutR = static_cast<float>(SRGBColor.R) / 255.0f;
            OutG = static_cast<float>(SRGBColor.G) / 255.0f;
            OutB = static_cast<float>(SRGBColor.B) / 255.0f;
        }
        else
        {
            OutR = FMath::Clamp(LinearColor.R, 0.0f, 1.0f);
            OutG = FMath::Clamp(LinearColor.G, 0.0f, 1.0f);
            OutB = FMath::Clamp(LinearColor.B, 0.0f, 1.0f);
        }
    }
}

bool ConvertLinearToNV12Planes(const TArray<FFloat16Color>& SourcePixels, const FIntPoint& Resolution, EPanoramaGamma GammaMode, FNV12PlaneBuffers& OutPlanes)
{
    const int32 Width = Resolution.X;
    const int32 Height = Resolution.Y;
    if (Width <= 0 || Height <= 0 || (Width % 2) != 0 || (Height % 2) != 0)
    {
        return false;
    }

    const int32 ExpectedPixelCount = Width * Height;
    if (SourcePixels.Num() != ExpectedPixelCount)
    {
        return false;
    }

    OutPlanes.Resolution = Resolution;
    OutPlanes.YPlane.SetNumUninitialized(ExpectedPixelCount);
    OutPlanes.UVPlane.SetNumUninitialized((Width * Height) / 2);

    const int32 BlockWidth = Width / 2;
    const int32 BlockHeight = Height / 2;

    TArray<float> UAccumulator;
    TArray<float> VAccumulator;
    TArray<int32> SampleCounts;
    UAccumulator.SetNumZeroed(BlockWidth * BlockHeight);
    VAccumulator.SetNumZeroed(BlockWidth * BlockHeight);
    SampleCounts.SetNumZeroed(BlockWidth * BlockHeight);

    uint8* YPlanePtr = OutPlanes.YPlane.GetData();

    for (int32 Y = 0; Y < Height; ++Y)
    {
        for (int32 X = 0; X < Width; ++X)
        {
            const int32 PixelIndex = Y * Width + X;
            const FFloat16Color& Pixel = SourcePixels[PixelIndex];

            float R;
            float G;
            float B;
            float A;
            ExtractGammaAdjustedRGB(Pixel, GammaMode, R, G, B, A);
            UE_UNUSED(A);

            const float YLinear = 0.2126f * R + 0.7152f * G + 0.0722f * B;
            const float ULinear = -0.1146f * R - 0.3854f * G + 0.5000f * B;
            const float VLinear = 0.5000f * R - 0.4542f * G - 0.0458f * B;

            const float YByte = 16.0f + 219.0f * YLinear;
            const float UByte = 128.0f + 224.0f * ULinear;
            const float VByte = 128.0f + 224.0f * VLinear;

            YPlanePtr[PixelIndex] = ClampToByte(YByte);

            const int32 BlockX = X / 2;
            const int32 BlockY = Y / 2;
            const int32 BlockIndex = BlockY * BlockWidth + BlockX;
            UAccumulator[BlockIndex] += ClampToByte(UByte);
            VAccumulator[BlockIndex] += ClampToByte(VByte);
            SampleCounts[BlockIndex] += 1;
        }
    }

    uint8* UVPlanePtr = OutPlanes.UVPlane.GetData();
    for (int32 BlockY = 0; BlockY < BlockHeight; ++BlockY)
    {
        uint8* RowPtr = UVPlanePtr + (BlockY * Width);
        for (int32 BlockX = 0; BlockX < BlockWidth; ++BlockX)
        {
            const int32 BlockIndex = BlockY * BlockWidth + BlockX;
            const int32 SampleCount = FMath::Max(1, SampleCounts[BlockIndex]);
            const float UAverage = UAccumulator[BlockIndex] / static_cast<float>(SampleCount);
            const float VAverage = VAccumulator[BlockIndex] / static_cast<float>(SampleCount);
            RowPtr[BlockX * 2] = ClampToByte(UAverage);
            RowPtr[BlockX * 2 + 1] = ClampToByte(VAverage);
        }
    }

    return true;
}

void CollapsePlanesToNV12(const FNV12PlaneBuffers& Planes, TArray<uint8>& OutData)
{
    OutData.SetNumUninitialized(Planes.YPlane.Num() + Planes.UVPlane.Num());
    if (Planes.YPlane.Num() > 0)
    {
        FMemory::Memcpy(OutData.GetData(), Planes.YPlane.GetData(), Planes.YPlane.Num());
    }
    if (Planes.UVPlane.Num() > 0)
    {
        FMemory::Memcpy(OutData.GetData() + Planes.YPlane.Num(), Planes.UVPlane.GetData(), Planes.UVPlane.Num());
    }
}

bool ConvertLinearToP010Planes(const TArray<FFloat16Color>& SourcePixels, const FIntPoint& Resolution, EPanoramaGamma GammaMode, FP010PlaneBuffers& OutPlanes)
{
    const int32 Width = Resolution.X;
    const int32 Height = Resolution.Y;
    if (Width <= 0 || Height <= 0 || (Width % 2) != 0 || (Height % 2) != 0)
    {
        return false;
    }

    const int32 ExpectedPixelCount = Width * Height;
    if (SourcePixels.Num() != ExpectedPixelCount)
    {
        return false;
    }

    OutPlanes.Resolution = Resolution;
    OutPlanes.YPlane.SetNumUninitialized(ExpectedPixelCount);
    OutPlanes.UVPlane.SetNumUninitialized((Width * Height) / 2);

    const int32 BlockWidth = Width / 2;
    const int32 BlockHeight = Height / 2;

    TArray<float> UAccumulator;
    TArray<float> VAccumulator;
    TArray<int32> SampleCounts;
    UAccumulator.SetNumZeroed(BlockWidth * BlockHeight);
    VAccumulator.SetNumZeroed(BlockWidth * BlockHeight);
    SampleCounts.SetNumZeroed(BlockWidth * BlockHeight);

    uint16* YPlanePtr = OutPlanes.YPlane.GetData();

    for (int32 Y = 0; Y < Height; ++Y)
    {
        for (int32 X = 0; X < Width; ++X)
        {
            const int32 PixelIndex = Y * Width + X;
            const FFloat16Color& Pixel = SourcePixels[PixelIndex];

            float R;
            float G;
            float B;
            float A;
            ExtractGammaAdjustedRGB(Pixel, GammaMode, R, G, B, A);
            UE_UNUSED(A);

            const float YLinear = 0.2126f * R + 0.7152f * G + 0.0722f * B;
            const float ULinear = -0.1146f * R - 0.3854f * G + 0.5000f * B;
            const float VLinear = 0.5000f * R - 0.4542f * G - 0.0458f * B;

            const float YTenBit = 64.0f + 876.0f * YLinear;
            const float UTenBit = 512.0f + 896.0f * ULinear;
            const float VTenBit = 512.0f + 896.0f * VLinear;

            YPlanePtr[PixelIndex] = ClampToTenBit(YTenBit);

            const int32 BlockX = X / 2;
            const int32 BlockY = Y / 2;
            const int32 BlockIndex = BlockY * BlockWidth + BlockX;
            UAccumulator[BlockIndex] += ClampToTenBit(UTenBit);
            VAccumulator[BlockIndex] += ClampToTenBit(VTenBit);
            SampleCounts[BlockIndex] += 1;
        }
    }

    uint16* UVPlanePtr = OutPlanes.UVPlane.GetData();
    for (int32 BlockY = 0; BlockY < BlockHeight; ++BlockY)
    {
        uint16* RowPtr = UVPlanePtr + (BlockY * Width);
        for (int32 BlockX = 0; BlockX < BlockWidth; ++BlockX)
        {
            const int32 BlockIndex = BlockY * BlockWidth + BlockX;
            const int32 SampleCount = FMath::Max(1, SampleCounts[BlockIndex]);
            const float UAverage = UAccumulator[BlockIndex] / static_cast<float>(SampleCount);
            const float VAverage = VAccumulator[BlockIndex] / static_cast<float>(SampleCount);
            RowPtr[BlockX * 2] = ClampToTenBit(UAverage);
            RowPtr[BlockX * 2 + 1] = ClampToTenBit(VAverage);
        }
    }

    return true;
}

void CollapsePlanesToP010(const FP010PlaneBuffers& Planes, TArray<uint8>& OutData)
{
    const int32 YBytes = Planes.YPlane.Num() * sizeof(uint16);
    const int32 UVBytes = Planes.UVPlane.Num() * sizeof(uint16);
    OutData.SetNumUninitialized(YBytes + UVBytes);
    if (YBytes > 0)
    {
        FMemory::Memcpy(OutData.GetData(), Planes.YPlane.GetData(), YBytes);
    }
    if (UVBytes > 0)
    {
        FMemory::Memcpy(OutData.GetData() + YBytes, Planes.UVPlane.GetData(), UVBytes);
    }
}

bool ConvertLinearToBGRAPayload(const TArray<FFloat16Color>& SourcePixels, const FIntPoint& Resolution, EPanoramaGamma GammaMode, TArray<uint8>& OutData)
{
    const int32 Width = Resolution.X;
    const int32 Height = Resolution.Y;
    if (Width <= 0 || Height <= 0)
    {
        return false;
    }

    const int32 ExpectedPixelCount = Width * Height;
    if (SourcePixels.Num() != ExpectedPixelCount)
    {
        return false;
    }

    OutData.SetNumUninitialized(ExpectedPixelCount * 4);

    uint8* DestPtr = OutData.GetData();
    for (int32 Index = 0; Index < ExpectedPixelCount; ++Index)
    {
        const FFloat16Color& Pixel = SourcePixels[Index];
        float R;
        float G;
        float B;
        float A;
        ExtractGammaAdjustedRGB(Pixel, GammaMode, R, G, B, A);

        DestPtr[Index * 4 + 0] = ClampToByte(B * 255.0f);
        DestPtr[Index * 4 + 1] = ClampToByte(G * 255.0f);
        DestPtr[Index * 4 + 2] = ClampToByte(R * 255.0f);
        DestPtr[Index * 4 + 3] = ClampToByte(A * 255.0f);
    }

    return true;
}

}
}


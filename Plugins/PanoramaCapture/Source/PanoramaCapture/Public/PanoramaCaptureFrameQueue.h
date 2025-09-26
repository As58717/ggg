#pragma once

#include "CoreMinimal.h"

/** Lock-free ring buffer queue for video frames. */
template <typename ElementType>
class TPanoramaFrameQueue
{
public:
    explicit TPanoramaFrameQueue(int32 InCapacity = 120)
        : Capacity(FMath::Max(1, InCapacity))
    {
        Storage.SetNumZeroed(Capacity);
    }

    bool Enqueue(const TSharedPtr<ElementType, ESPMode::ThreadSafe>& Item)
    {
        FScopeLock Lock(&CriticalSection);
        const int32 NextHead = (Head + 1) % Capacity;
        if (NextHead == Tail)
        {
            ++Dropped;
            return false;
        }

        Storage[Head] = Item;
        Head = NextHead;
        ++Count;
        return true;
    }

    TSharedPtr<ElementType, ESPMode::ThreadSafe> Dequeue()
    {
        FScopeLock Lock(&CriticalSection);
        if (Tail == Head)
        {
            return nullptr;
        }

        const TSharedPtr<ElementType, ESPMode::ThreadSafe> Item = Storage[Tail];
        Storage[Tail].Reset();
        Tail = (Tail + 1) % Capacity;
        --Count;
        return Item;
    }

    void Reset()
    {
        FScopeLock Lock(&CriticalSection);
        for (int32 Index = 0; Index < Capacity; ++Index)
        {
            Storage[Index].Reset();
        }
        Head = 0;
        Tail = 0;
        Count = 0;
        Dropped = 0;
    }

    int32 Num() const
    {
        FScopeLock Lock(&CriticalSection);
        return Count;
    }

    int32 GetCapacity() const
    {
        return Capacity;
    }

    int32 GetDroppedCount() const
    {
        FScopeLock Lock(&CriticalSection);
        return Dropped;
    }

private:
    mutable FCriticalSection CriticalSection;
    TArray<TSharedPtr<ElementType, ESPMode::ThreadSafe>> Storage;
    int32 Head = 0;
    int32 Tail = 0;
    int32 Count = 0;
    int32 Capacity = 0;
    int32 Dropped = 0;
};

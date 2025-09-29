# Compose 斗地主

使用 Kotlin + Jetpack Compose + MVVM 实现的本地斗地主示例，支持三人/四人模式与两种难度 AI。

## 功能概览
- 完整的斗地主发牌、叫分/抢地主、定地主、出牌轮转、牌型校验（含炸弹、火箭、飞机等）。
- 春天、炸弹、火箭倍数规则。
- sealed class 构建的流程状态机（洗牌→发牌→叫分→定地主→出牌→结算）。
- 横屏 Compose UI，真人对战多名 AI，支持提示、出牌、过牌。
- 简单/思考两个难度机器人，含贪心与牌型推断。
- 占位用的牌面矢量图标与音效资源。

## 构建与运行
1. 使用 **Android Studio Giraffe (或更高)** 打开 `android/doudizhu` 目录。
2. 同步 Gradle（初次可能需要下载依赖）。
3. 连接或启动一台 **Android 8.0 (API 26)** 及以上的设备/模拟器（本项目 `minSdk=24`，推荐 API 26+）。
4. 运行 `app` 模块即可进入游戏。

### 主要配置
- `minSdk = 24`
- `targetSdk = 34`
- Kotlin 1.9.22
- Compose Compiler 1.5.8
- Android Gradle Plugin 8.2.2

## AIDE 编译提示
如使用 AIDE，可将 `android/doudizhu` 复制至设备本地，确保启用 Java 17 编译链，并在 `gradle.properties` 中保留 `org.gradle.jvmargs` 设置，首次构建可能耗时较长。

## 后续扩展建议
- 扩展 `GameViewModel` 中的 `GameEvent` 与 `GamePhase` 支持联机同步。
- 在 `BotStrategies` 中加入记牌器与概率推断，提升 AI 水平。
- 利用 `GameUiState` 的 `audioCue` 钩子替换真实音效或接入联网语音播报。
- 新增局域网/云对战时，可以将 `GameEngine` 的状态输出同步到网络层。

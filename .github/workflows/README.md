# RawLab GitHub Actions IPA 构建说明

工作流不需要 Apple Developer 证书、Provisioning Profile 或 GitHub Secrets。

每次手动运行或 push 到 `main` 后，GitHub Actions 会在 macOS runner 上：

1. 下载并校验 LibRaw 0.21.5
2. 为 iPhoneOS 和 arm64 Simulator 编译 LibRaw
3. 编译 `sony2fuji.framework`
4. 生成 `sony2fuji.xcframework`
5. 编译并 Archive `RawLab.xcodeproj`
6. 打包成 `RawLab.ipa`
7. 上传 Artifact

## 使用方式

进入：

```text
Actions → Build RawLab iOS IPA → Run workflow
```

等待完成后，在页面最下方下载：

```text
RawLab-ipa-<run number>
```

里面包含：

```text
RawLab.ipa
xcode-archive.log
```

## 本地签名

这个 IPA 是未签名的“待签名 IPA”，不能直接安装。下载后使用你自己的证书和 profile 签名，例如使用 SideStore、AltStore、Sideloadly、ESign 或其他本地重签工具。

重签时通常需要：

- Apple Development / Distribution certificate
- 与你账号匹配的 provisioning profile
- 你自己的 Bundle ID
- 必要时重新设置 App Entitlements

建议使用支持“替换 Bundle ID 并重签嵌入 Framework”的工具，因为 RawLab 包含原生 `sony2fuji.framework`。

## 重要说明

- 构建工作流本身不读取任何证书或 Secrets。
- GitHub Artifact 默认保存 14 天，请及时下载。
- `RawLab.ipa` 未签名，iOS 不会直接安装。
- 由于工程的默认 Bundle ID 是 `com.example.RawLab`，本地重签时应改成你自己的 App ID。
- 工作流只构建 arm64 真机和 arm64 模拟器 slice；真实 iPhone IPA 使用真机 arm64 slice。
- 不要把 `.p12`、`.mobileprovision`、`.pem` 或 `.key` 上传到仓库。

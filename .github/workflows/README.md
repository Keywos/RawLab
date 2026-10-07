# RawLab GitHub Actions IPA 构建说明

将 `build-ipa.yml` 复制到 Fork 仓库的：

```text
.github/workflows/build-ipa.yml
```

工作流会在 macOS runner 上：

1. 下载并校验 LibRaw 0.21.5
2. 为 iPhoneOS 和 arm64 Simulator 编译 LibRaw
3. 编译 `sony2fuji.framework`
4. 生成 `lutools/build-ios/sony2fuji.xcframework`
5. 编译 `RawLab.xcodeproj`
6. 上传 unsigned 或 signed IPA

## 第一次建议

先从 GitHub Actions 页面手动运行 `unsigned`。它只能用于验证编译链，不能直接安装到 iPhone。

编译通过后，再配置签名 Secrets 和仓库变量，手动运行 `signed`。

## 必需的 signed Secrets

- `IOS_CERTIFICATE_P12_B64`：Apple Distribution 或 Apple Development 证书的 Base64 内容
- `IOS_CERTIFICATE_PASSWORD`：P12 导出密码
- `IOS_PROVISIONING_PROFILE_B64`：与 Bundle ID、证书和设备匹配的 provisioning profile Base64 内容
- `IOS_KEYCHAIN_PASSWORD`：临时 CI keychain 密码
- `IOS_TEAM_ID`：Apple Developer Team ID

仓库变量：

- `IOS_BUNDLE_ID`：必须和 provisioning profile 的 App ID 完全匹配，例如 `com.example.RawLab`
- 可选 `RAWLAB_SIGNING_MODE=signed`，设为后 push 自动构建签名 IPA

## 导出证书和 profile

```bash
base64 -i RawLabDistribution.p12 | pbcopy
base64 -i RawLab.mobileprovision | pbcopy
```

不要把 `.p12`、`.mobileprovision`、密码或私钥提交到 Git。

## 重要限制

- `unsigned.ipa` 不能安装，只能确认 archive/package 流程。
- signed IPA 的 profile 必须包含目标 iPhone 的 UDID（Ad Hoc），或者使用适配的签名类型。
- GitHub-hosted runner 每次都是干净机器，证书和 profile 必须通过 Secrets 注入。
- 工作流目前只构建 arm64 真机和 arm64 模拟器 slice；iPhone 真机 IPA 只需要 arm64。
- 若 GitHub runner 的 Xcode 版本与项目不兼容，可在工作流中加入 `xcode-select` 或改用固定 macOS/Xcode runner。
- 原仓库 `lutools/build.sh ios` 依赖预先存在的 iOS LibRaw 安装目录，工作流已补上这个缺口。

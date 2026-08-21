# JamesDSP Controller Windows Release Signing

The release package does not contain certificate files, passwords, private
keys, or fixed certificate thumbprints.

Prerequisites:

- Windows SDK `signtool.exe`;
- a trusted code-signing certificate in `CurrentUser\My` or `LocalMachine\My`;
- access to the configured RFC 3161 timestamp service.

Sign the package binaries and latest installer:

```powershell
.\sign_release.ps1 -CertificateThumbprint "<certificate thumbprint>"
```

Verify existing signatures without changing files:

```powershell
.\sign_release.ps1 -VerifyOnly
```

Signing must occur after the final package and installer build. Any rebuild
invalidates or replaces signed artifacts and requires signing again.

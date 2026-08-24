# K-Lite Codec Pack Full runtime bootstrap

Mathery Kadia requires K-Lite Codec Pack Full.

Startup behavior:

1. Check the standard K-Lite uninstall registry key for a Full edition.
2. If present, continue immediately.
3. If absent, show Kadia's own borderless progress window.
4. Download the Windows-compatible Full installer from `files2.codecguide.com`.
5. Verify the downloaded EXE with the pinned SHA-256 digest.
6. Launch the official installer with `/verysilent /norestart`.
7. Wait for setup to exit and verify that the Full edition is now registered.
8. Delete the temporary installer and start the main Kadia UI.

Pinned packages:

- XP SP3: 13.8.5 Full
- Vista: 16.7.6 Full
- Windows 7+: 19.9.0 Full

The download phase reports real bytes. The installation phase uses Kadia's own activity progress because Inno Setup does not expose a silent-install percentage to the parent process.

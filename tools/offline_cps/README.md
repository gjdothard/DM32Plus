<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 Garrett Dothard -->
# RadioFoundry Offline CPS

For Windows 10/11, 64-bit Intel or AMD PCs.

1. Copy the ZIP to the laptop and choose **Extract All**. Keep all the files together.
2. Double-click **Start CPS.cmd**. The CPS opens in Microsoft Edge.
3. Keep the small server window open while using the CPS. Close it to stop the app.

No Python installation, administrator access, or internet connection is needed to start the CPS.
If the browser does not open, use Edge or Chrome to visit **http://localhost:8765/codeplug/**.

The product installer remembers the selected release channel. **Bundled (offline)** uses the image in
the package. **Stable** uses the newest published stable release, and **Development** uses the newest
published prerelease. The online channels require internet access. The installer displays the target
version before proceeding.
Use Edge or Chrome for the browser serial connection used for radio programming.

## Offline use

- Read and program a supported radio with its USB cable.
- Edit channels, zones, contacts, encryption settings, and other codeplug settings.
- Open and save codeplug files. Import a locally saved contact database.
- Use the bundled documentation and the web updater with locally saved firmware files.

Radio ID database downloads, online update checks, cloud firmware builds, and community links need internet.
The package contains no personal codeplug, browser draft, or encryption keys. To move an existing codeplug,
export it from the current CPS and open that file on the laptop. Stored radio encryption keys remain on the radio.

The programming cable's Windows driver must already be installed. If Windows does not show a serial port when
you connect the cable, install its manufacturer's driver before using the laptop offline.

## Saved work and connections

Use the same browser profile and port each time to keep the browser's saved draft. Export a file as a backup.
Only one program can use the radio's serial port at a time. Disconnect other CPS or updater sessions first.
Click **Connect** and select the radio's port; the browser asks for permission to use it.

The server listens only on this computer. It does not connect to or program the radio automatically.
Starting the same copy twice reopens the existing app. If a different app occupies port 8765, close it or run
`Start CPS.cmd --port 8766`; that port has its own browser draft.

The portable runtime's license is in `runtime/LICENSE.txt`. RadioFoundry's license is in `LICENSE-RadioFoundry.txt`.
`build-info.json` identifies this package; `SHA256SUMS` lists its file checksums.

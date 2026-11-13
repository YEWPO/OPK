# Soundfont Import Guide

[简体中文](soundfont.zh-CN.md)

OpenKeys Piano does not bundle or automatically download soundfonts. Before playing, prepare an **SF2 soundfont file** and choose **Import Soundfont**. A soundfont supplies the instrument's sound. Without one, you can edit key mappings and open performance files, but cannot produce audio or record yet.

## 1. Obtain a soundfont

If you already have a piano soundfont, use its `.sf2` file. Otherwise, choose a soundfont yourself from the [official FreePats piano page](https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html):

| Soundfont | Suitable for | What to download |
| --- | --- | --- |
| Upright Piano KW | A smaller download and lower memory use to get started | The SF2 row's Sound bank; the standard archive is approximately 27 MiB |
| Salamander Grand Piano | More detailed tone and velocity layers with larger files | The SF2 row's Sound bank; the archive is approximately 296 MiB |

The page also offers formats such as SFZ. Select **SF2**. Links open your browser; OpenKeys does not download or distribute these banks. Consult the provider's page and archive for authorship and licensing. Download sizes and available versions may change.

## 2. Extract the download

If the download is a `.zip`, `.tar.xz` or another archive, extract it completely with a suitable archive tool, then locate the `.sf2` file.

Do not select the archive, an `.sfz` file or a VST plugin's `.dll` / `.vst3`. Renaming a file to `.sf2` does not change its format.

## 3. Import in OpenKeys Piano

1. Choose **Import Soundfont**.
2. Select the extracted `.sf2` file and confirm.
3. Wait for copying and loading to finish. Large banks take longer; the application displays progress.
4. Once the soundfont name appears, play with your computer keyboard, or choose **Demo**, then **Play**.

The application saves a copy in its data directory. **After a successful import, you may remove or move the original download.** The managed copy loads automatically next time.

Copying and loading can require substantial disk space and memory. During replacement, old and new copies temporarily coexist; leave enough free space.

## 4. Replace a soundfont

Choose **Import Soundfont** again and select another SF2. Only one current bank is kept: after the new bank loads successfully, settings are updated and the previous managed copy is removed. Original downloads are untouched.

If the new file is damaged or unreadable, or storage is full, an error explains the problem and the previous soundfont and settings are preserved. External soundfont paths saved by older versions require one explicit import to create a managed copy.

## Troubleshooting

**The downloaded file is missing from the file picker.**

Extract it first and select an `.sf2` file. The importer does not support SFZ, SF3 or VST plugins.

**The application reports an incomplete SF2 or cannot load it.**

Check that the download and extraction finished. Download again or try a smaller piano SF2. Check available memory and disk space for large banks.

**There is no sound after importing.**

Check that the bank name is displayed, master volume is above zero and the system is not muted. Choose the correct output device in **Audio Settings**, then click the keyboard area to focus it. Reopen Audio Settings to reconnect a disconnected device.

**There are clicks, dropouts or noticeable latency.**

Try a smaller bank. Increase the buffer in **Audio Settings** to reduce dropouts; decreasing it may reduce latency but requires better device performance.

**The bank does not sound like a piano.**

The current version uses the first preset in the file. Prefer a piano-only SF2.

**The application asks to import a previously imported bank again.**

The managed copy may have been removed or damaged. Select an SF2 again. External paths recorded by older versions also need one explicit import.

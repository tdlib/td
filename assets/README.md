# Assets Directory

This directory contains branding assets for TDLib.

## Contents

Place your custom branding files here:

- **logo.png** - Main logo displayed on the build instructions page (recommended max height: 60px)
- **icon.png** - Icon for favicons or other small branding elements

## File Formats

Supported image formats:
- PNG (recommended for logos with transparency)
- SVG (recommended for scalable graphics)
- JPG
- GIF

## Configuration

Update the `branding.json` file in the root directory to reference your assets:

```json
{
  "assets": {
    "logo": "assets/logo.png",
    "icon": "assets/icon.png",
    "logoAlt": "Your Logo Alt Text"
  }
}
```

## Example Assets

You can use any image editing software to create your branding assets:
- Adobe Photoshop
- GIMP (free)
- Figma (free for personal use)
- Canva (free)

For best results:
- Logo: 200-400px wide, 50-60px tall
- Icon: 512x512px square
- Use transparent backgrounds (PNG format)

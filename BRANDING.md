# TDLib Branding Customization

This document explains how to customize the colors, logo, and branding for TDLib's build instructions page.

## Overview

TDLib now supports customizable branding through a `branding.json` configuration file. You can define:

- **Primary and secondary colors** for the UI
- **Logo and icon** assets
- **Branding name** and description

## Configuration File

The branding configuration is stored in `branding.json` at the root of the repository.

### Default Configuration

```json
{
  "colors": {
    "primary": "#0088ff",
    "primaryLight": "#42a7ff",
    "secondary": "#6c757d",
    "secondaryLight": "#95999c"
  },
  "assets": {
    "logo": "assets/logo.png",
    "icon": "assets/icon.png",
    "logoAlt": "TDLib Logo"
  },
  "branding": {
    "name": "TDLib",
    "description": "Telegram Database Library"
  }
}
```

## Customization Guide

### 1. Colors

#### Primary Color
The primary color is used for:
- Links and hyperlinks
- Focus states on interactive elements
- Checked checkboxes and radio buttons
- Selected states

```json
"colors": {
  "primary": "#0088ff",
  "primaryLight": "#42a7ff"
}
```

**Example custom colors:**
- Blue: `"primary": "#0088ff"`, `"primaryLight": "#42a7ff"`
- Green: `"primary": "#28a745"`, `"primaryLight": "#5cb85c"`
- Purple: `"primary": "#6f42c1"`, `"primaryLight": "#9b72d6"`
- Red: `"primary": "#dc3545"`, `"primaryLight": "#e46573"`

#### Secondary Color
The secondary color is used for:
- Secondary UI elements
- Borders and dividers
- Less prominent interactive elements

```json
"colors": {
  "secondary": "#6c757d",
  "secondaryLight": "#95999c"
}
```

### 2. Logo and Icon

#### Logo
The logo is displayed at the top of the build instructions page.

```json
"assets": {
  "logo": "assets/logo.png",
  "logoAlt": "TDLib Logo"
}
```

**Requirements:**
- Supported formats: PNG, SVG, JPG, GIF
- Recommended max height: 60px
- Path is relative to the root directory

#### Icon
The icon can be used for favicons or other small branding elements.

```json
"assets": {
  "icon": "assets/icon.png"
}
```

### 3. Branding Name

The branding name appears in the page title.

```json
"branding": {
  "name": "TDLib",
  "description": "Telegram Database Library"
}
```

## How It Works

The `build.html` page includes a JavaScript function that:

1. Fetches `branding.json` on page load
2. Applies custom colors to CSS variables
3. Updates the page title with the branding name
4. Inserts the logo image if configured

If `branding.json` is not found or fails to load, the page falls back to default values.

## Example: Custom Branding

Here's an example configuration for a custom Telegram client:

```json
{
  "colors": {
    "primary": "#2aabee",
    "primaryLight": "#5ec3f7",
    "secondary": "#8e8e93",
    "secondaryLight": "#aeaeb2"
  },
  "assets": {
    "logo": "assets/my-client-logo.png",
    "icon": "assets/my-client-icon.png",
    "logoAlt": "My Telegram Client"
  },
  "branding": {
    "name": "My Telegram Client",
    "description": "Custom Telegram client built with TDLib"
  }
}
```

## Testing Your Branding

1. Edit `branding.json` with your custom values
2. Open `build.html` in a web browser
3. Verify that:
   - Your logo appears at the top
   - The page title shows your branding name
   - Links and interactive elements use your primary color
   - The color scheme matches your brand

## Dark Mode

The build instructions page supports dark mode through CSS media queries. Colors in `branding.json` apply to both light and dark modes. If you need different colors for dark mode, you would need to modify the CSS in `build.html` directly.

## Asset Directory Structure

It's recommended to store your branding assets in an `assets/` directory:

```
td/
├── branding.json
├── build.html
└── assets/
    ├── logo.png
    └── icon.png
```

## Troubleshooting

**Logo not appearing:**
- Check that the path in `branding.json` is correct and relative to the root directory
- Verify the image file exists and is accessible
- Check browser console for errors

**Colors not applying:**
- Ensure color values are valid CSS hex colors (e.g., `#0088ff`)
- Clear browser cache and reload the page
- Check browser console for JSON parsing errors

**Page title not updating:**
- Verify the `branding.name` field is set in `branding.json`
- Check that the JSON is valid (no syntax errors)

## Contributing

If you have suggestions for improving the branding system, please open an issue or pull request on the TDLib repository.

# Third-Party Notices

KomaPaint links against the Qt framework (https://www.qt.io/).

Qt is available under the GNU Lesser General Public License v3 as well as
under commercial licenses. KomaPaint uses Qt in accordance with the terms
of the LGPL v3 during development; the final distribution license will be
determined before any public binary release.

## Spectral.js

The colour mixer includes a Qt-compatible adaptation of Spectral.js 3.0.0
(commit bb2b05c9d1e65ae824d47e3b1cc17ea32c8ee68f),
https://github.com/rvanwijnen/spectral.js. It uses spectral approximations
and Kubelka–Munk theory for pigment-inspired mixing.

MIT License. Copyright (c) 2025 Ronald van Wijnen. The full permission and
warranty notice appears at the top of `ui/components/SpectralMix.js`.
The script was transpiled for Qt's JavaScript engine, its module wrapper was
adapted for QML, and forward declarations were added to avoid Qt warnings.

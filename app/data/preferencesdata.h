/*
 * Copyright 2020  Michail Vourlakos <mvourlakos@gmail.com>
 *
 * This file is part of Latte-Dock
 *
 * Latte-Dock is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * Latte-Dock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef PREFERENCESDATA_H
#define PREFERENCESDATA_H

// local
#include "../apptypes.h"

namespace Latte {
namespace Data {

class Preferences
{
public:
    static const bool BADGE3DSTYLE = true;
    static const bool LAYOUTSINFORMATIONWINDOW = true;
    static const bool AUTOSTART = true;
    static const bool BORDERLESSMAXIMIZED = false;
    static const bool METAPRESSFORAPPLAUNCHER = false;
    static const bool METAHOLDFORBADGES = true;
    static const int SCREENSDELAY = 2500;
    static const Settings::MouseSensitivity MOUSESENSITIVITY = Settings::HighMouseSensitivity;

    Preferences();
    Preferences(Preferences &&o);
    Preferences(const Preferences &o);

    //! Preferences data
    bool badgeStyle3D{BADGE3DSTYLE};
    bool layoutsInformationWindow{LAYOUTSINFORMATIONWINDOW};
    bool autostart{AUTOSTART};
    bool borderlessMaximized{BORDERLESSMAXIMIZED};
    bool metaPressForAppLauncher{METAPRESSFORAPPLAUNCHER};
    bool metaHoldForBadges{METAHOLDFORBADGES};
    int screensDelay{SCREENSDELAY};
    Settings::MouseSensitivity mouseSensitivity{MOUSESENSITIVITY};

    bool inDefaultValues() const;
    void setToDefaults();

    //! Operators
    Preferences &operator=(const Preferences &rhs);
    Preferences &operator=(Preferences &&rhs);
    bool operator==(const Preferences &rhs) const;
    bool operator!=(const Preferences &rhs) const;
};

}
}

#endif

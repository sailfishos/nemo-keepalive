/* ------------------------------------------------------------------------- *
 * Copyright (C) 2014 - 2018 Jolla Ltd.
 *
 * Author: Martin Jones <martin.jones@jollamobile.com>
 * Author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
 *
 * License: BSD
 * ------------------------------------------------------------------------- */

import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.KeepAlive 1.2

ApplicationWindow {
    initialPage: Component {
        Page {
            property string displayStatus: displayBlanking1.status == DisplayBlanking.Off ? "off"
                                           : (displayBlanking1.status == DisplayBlanking.Dimmed ? "dimmed" : "on")

            onDisplayStatusChanged: console.log("Display blanking status:", displayStatus)

            Column {
                width: parent.width
                spacing: Theme.paddingLarge

                TextSwitch {
                    text: "Prevent blanking1"
                    onCheckedChanged: displayBlanking1.preventBlanking = checked
                }

                TextSwitch {
                    text: "Prevent blanking2"
                    onCheckedChanged: displayBlanking2.preventBlanking = checked
                }

                TextSwitch {
                    text: "Prevent blanking3"
                    onCheckedChanged: displayBlanking3.preventBlanking = checked
                }

                Label {
                    x: Theme.paddingLarge
                    text: "Display is " + displayStatus
                }
            }

            DisplayBlanking {
                id: displayBlanking1
            }

            DisplayBlanking {
                id: displayBlanking2
            }

            DisplayBlanking {
                id: displayBlanking3
            }
        }
    }
}

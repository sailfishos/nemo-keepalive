/* ------------------------------------------------------------------------- *
 * Copyright (C) 2014 - 2019 Jolla Ltd.
 *
 * Author: Martin Jones <martin.jones@jollamobile.com>
 * Author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
 *
 * License: BSD
 * ------------------------------------------------------------------------- */

/* NOTE: This qml page exists solely for the purpose of having
 *       something readily available for testing/debugging backwards
 *       compatibility with various DisplayBlanking interface versions.
 */

import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.KeepAlive 1.1

Page {
    property string displayStatus: DisplayBlanking.status == DisplayBlanking.Off ? "off"
    : (DisplayBlanking.status == DisplayBlanking.Dimmed ? "dimmed" : "on")

    onDisplayStatusChanged: console.log("Display blanking status:", displayStatus)

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Nemo.KeepAlive 1.1"
        }

        TextSwitch {
            text: "Prevent blanking"
            checked: DisplayBlanking.preventBlanking
            onCheckedChanged: DisplayBlanking.preventBlanking = checked
        }

        Label {
            x: Theme.paddingLarge
            text: "Display is " + displayStatus
        }
    }
}

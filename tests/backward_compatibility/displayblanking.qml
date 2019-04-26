/* ------------------------------------------------------------------------- *
 * Copyright (C) 2014 - 2019 Jolla Ltd.
 *
 * Author: Martin Jones <martin.jones@jollamobile.com>
 * Author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
 *
 * License: BSD
 * ------------------------------------------------------------------------- */


/* NOTE: This application exists solely for the purpose of having
 *       something readily available for testing/debugging backwards
 *       compatibility with various DisplayBlanking interface versions.
 */

import QtQuick 2.0
import Sailfish.Silica 1.0

ApplicationWindow {
    initialPage: Component {
        Page {
            Column {
                width: parent.width
                spacing: Theme.paddingSmall
                anchors.centerIn: parent

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Blank prevent test app"
                }

                Button {
                    text: "API 1.0 test page"
                    anchors.horizontalCenter: parent.horizontalCenter
                    onClicked: pageStack.push("BlankingPage10.qml")
                }

                Button {
                    text: "API 1.1 test page"
                    anchors.horizontalCenter: parent.horizontalCenter
                    onClicked: pageStack.push("BlankingPage11.qml")
                }

                Button {
                    text: "API 1.2 test page"
                    anchors.horizontalCenter: parent.horizontalCenter
                    onClicked: pageStack.push("BlankingPage12.qml")
                }
            }
        }
    }
}

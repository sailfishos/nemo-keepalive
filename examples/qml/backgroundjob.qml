/* ------------------------------------------------------------------------- *
 * Copyright (C) 2014 - 2018 Jolla Ltd.
 *
 * Author: Martin.Jones <martin.jones@jollamobile.com>
 * Author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
 *
 * License: BSD
 * ------------------------------------------------------------------------- */

import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.KeepAlive 1.1

ApplicationWindow {
    initialPage: Component {
        Page {
            Timer {
                id: timer
                interval: 1000
                repeat: true
                property int run_periods
                onTriggered: {
                    if (--run_periods == 0) {
                        backgroundJob.finished()
                        stop()
                    }
                }
            }

            BackgroundJob {
                id: backgroundJob
                frequency: BackgroundJob.ThirtySeconds
                onTriggered: {
                    timer.run_periods = 5
                    timer.running = true
                }
            }

            Column {
                width: parent.width
                spacing: Theme.paddingLarge
                anchors.centerIn: parent
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    opacity: backgroundJob.enabled ? 1.0 : 0.0
                    text: backgroundJob.running ? "running job " + timer.run_periods : "job waiting"
                }
                Button {
                    text: backgroundJob.enabled ? "Enabled" : "Disabled"
                    anchors.horizontalCenter: parent.horizontalCenter
                    onClicked: {
                        backgroundJob.enabled = !backgroundJob.enabled
                    }
                }
                Button {
                    text: backgroundJob.triggeredOnEnable ? "Trigger on enable" : "Wait for trigger"
                    anchors.horizontalCenter: parent.horizontalCenter
                    onClicked: {
                        backgroundJob.triggeredOnEnable = !backgroundJob.triggeredOnEnable
                    }
                }
            }
        }
    }
}

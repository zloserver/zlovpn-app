import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Config"

PageType {
    id: root

    defaultActiveFocusItem: focusItem

    property bool isAppSplitTinnelingEnabled: Qt.platform.os === "windows" || Qt.platform.os === "android"

    Item {
        id: focusItem
        KeyNavigation.tab: backButton
    }

    BackButtonType {
        id: backButton

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20

        KeyNavigation.tab: dnsServersButton.rightButton
    }

    FlickableType {
        id: fl
        anchors.top: backButton.bottom
        anchors.bottom: parent.bottom
        contentHeight: content.height

        ColumnLayout {
            id: content

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right

            HeaderType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("Connection")
            }

            DividerType {}

            LabelWithButtonType {
                id: dnsServersButton
                Layout.fillWidth: true

                text: qsTr("DNS servers")
                descriptionText: qsTr("DNS servers to use")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsDns)
                }

                KeyNavigation.tab: splitTunnelingButton.rightButton
            }

            DividerType {}

            LabelWithButtonType {
                id: splitTunnelingButton
                Layout.fillWidth: true

                text: qsTr("Site-based split tunneling")
                descriptionText: qsTr("Allows you to select which sites you want to access through the VPN")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsSplitTunneling)
                }

                Keys.onTabPressed: {
                    if (splitTunnelingButton2.visible) {
                        return splitTunnelingButton2.rightButton.forceActiveFocus()
                    } else if (killSwitchSwitcher.visible) {
                        return killSwitchSwitcher.forceActiveFocus()
                    } else {
                        lastItemTabClicked()
                    }
                }
            }

            DividerType {
                visible: root.isAppSplitTinnelingEnabled
            }

            LabelWithButtonType {
                id: splitTunnelingButton2
                visible: root.isAppSplitTinnelingEnabled

                Layout.fillWidth: true

                text: qsTr("App-based split tunneling")
                descriptionText: qsTr("Allows you to use the VPN only for certain Apps")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsAppSplitTunneling)
                }

                Keys.onTabPressed: {
                    if (killSwitchSwitcher.visible) {
                        return killSwitchSwitcher.forceActiveFocus()
                    } else {
                        lastItemTabClicked()
                    }
                }
            }

            DividerType {
                visible: root.isAppSplitTinnelingEnabled
            }

            SwitcherType {
                id: excludeLocalNetworksSwitcher

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Exclude local networks")
                descriptionText: qsTr("The router, printers and other devices of your home network stay reachable while the VPN is on")

                checked: SettingsController.isExcludeLocalNetworksEnabled()
                checkable: !ConnectionController.isConnected
                onCheckedChanged: {
                    if (checked !== SettingsController.isExcludeLocalNetworksEnabled()) {
                        SettingsController.toggleExcludeLocalNetworks(checked)
                    }
                }
                onClicked: {
                    if (!checkable) {
                        PageController.showNotificationMessage(qsTr("Cannot change this setting during active connection"))
                    }
                }
            }

            DividerType {}

            SwitcherType {
                id: killSwitchSwitcher
                visible: !GC.isMobile()

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("KillSwitch")
                descriptionText: qsTr("Disables your internet if your encrypted VPN connection drops out for any reason.")

                checked: SettingsController.isKillSwitchEnabled()
                checkable: !ConnectionController.isConnected
                onCheckedChanged: {
                    if (checked !== SettingsController.isKillSwitchEnabled()) {
                        SettingsController.toggleKillSwitch(checked)
                    }
                }
                onClicked: {
                    if (!checkable) {
                        PageController.showNotificationMessage(qsTr("Cannot change killSwitch settings during active connection"))
                    }
                }

                Keys.onTabPressed: lastItemTabClicked()
            }

            DividerType {
                visible: GC.isDesktop()
            }
        }
    }
}

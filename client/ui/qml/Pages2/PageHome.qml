import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import SortFilterProxyModel 0.2

import PageEnum 1.0
import ProtocolEnum 1.0
import ContainerProps 1.0
import ContainersModelFilters 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    defaultActiveFocusItem: focusItem

    Connections {
        target: PageController

        function onRestorePageHomeState(isContainerInstalled) {
            drawer.open()
            if (isContainerInstalled) {
                containersDropDown.rootButtonClickedFunction()
            }
        }
    }

    Item {
        anchors.fill: parent
        anchors.bottomMargin: drawer.collapsedHeight

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 34
            anchors.bottomMargin: 34

            Item {
                id: focusItem
                KeyNavigation.tab: loggingButton.visible ?
                                       loggingButton :
                                       connectButton
            }

            BasicButtonType {
                id: loggingButton
                property bool isLoggingEnabled: SettingsController.isLoggingEnabled

                Layout.alignment: Qt.AlignHCenter

                implicitHeight: 36

                defaultColor: AmneziaStyle.color.transparent
                hoveredColor: AmneziaStyle.color.translucentWhite
                pressedColor: AmneziaStyle.color.sheerWhite
                disabledColor: AmneziaStyle.color.mutedGray
                textColor: AmneziaStyle.color.mutedGray
                borderWidth: 0

                visible: isLoggingEnabled ? true : false
                text: qsTr("Logging enabled")

                Keys.onEnterPressed: loggingButton.clicked()
                Keys.onReturnPressed: loggingButton.clicked()

                KeyNavigation.tab: connectButton

                onClicked: {
                    PageController.goToPage(PageEnum.PageSettingsLogging)
                }
            }

            Header2TextType {
                id: remainingBalanceHeader

                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                color: AmneziaStyle.color.mutedGray
                font.pixelSize: 14
                font.weight: 500

                maximumLineCount: 2

                text: {
                    if (!AuthController.userInfo.isValid)
                        return AuthController.offlineMode ? "" : qsTr("...")
                    // without the API this is the last known balance, so say when it was known
                    return AuthController.offlineMode
                            ? qsTr("Balance on %1: %2").arg(AuthController.offlineDataDate).arg(AuthController.userInfo.localizedTimeLeft)
                            : qsTr("Balance: ") + AuthController.userInfo.localizedTimeLeft
                }
                horizontalAlignment: Qt.AlignHCenter

                KeyNavigation.tab: tabBar
            }

            RowLayout {
                id: offlineBanner
                visible: AuthController.offlineMode

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                spacing: 8

                WarningType {
                    Layout.fillWidth: true

                    iconPath: "qrc:/images/controls/alert-circle.svg"
                    imageColor: AmneziaStyle.color.goldenApricot
                    textString: qsTr("No connection to the ZloVPN server, using saved settings")
                }

                ImageButtonType {
                    implicitWidth: 40
                    implicitHeight: 40

                    image: "qrc:/images/controls/refresh-cw.svg"
                    imageColor: AmneziaStyle.color.goldenApricot

                    onClicked: AuthController.findServer()
                }
            }

            ConnectButton {
                id: connectButton
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignCenter
                KeyNavigation.tab: splitTunnelingButton
            }

            BasicButtonType {
                id: splitTunnelingButton

                Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
                Layout.bottomMargin: 34
                leftPadding: 16
                rightPadding: 16

                implicitHeight: 36

                defaultColor: AmneziaStyle.color.transparent
                hoveredColor: AmneziaStyle.color.translucentWhite
                pressedColor: AmneziaStyle.color.sheerWhite
                disabledColor: AmneziaStyle.color.mutedGray
                textColor: AmneziaStyle.color.mutedGray
                leftImageColor: AmneziaStyle.color.transparent
                borderWidth: 0

                buttonTextLabel.lineHeight: 20
                buttonTextLabel.font.pixelSize: 14
                buttonTextLabel.font.weight: 500

                property bool isSplitTunnelingEnabled: SitesModel.isTunnelingEnabled || AppSplitTunnelingModel.isTunnelingEnabled

                text: isSplitTunnelingEnabled ? qsTr("Split tunneling enabled") : qsTr("Split tunneling disabled")

                imageSource: isSplitTunnelingEnabled ? "qrc:/images/controls/split-tunneling.svg" : ""
                rightImageSource: "qrc:/images/controls/chevron-down.svg"

                Keys.onEnterPressed: splitTunnelingButton.clicked()
                Keys.onReturnPressed: splitTunnelingButton.clicked()

                KeyNavigation.tab: drawer

                onClicked: {
                    homeSplitTunnelingDrawer.open()
                }

                HomeSplitTunnelingDrawer {
                    id: homeSplitTunnelingDrawer
                    parent: root

                    onClosed: {
                        if (!GC.isMobile()) {
                            focusItem.forceActiveFocus()
                        }
                    }
                }
            }
        }
    }


    DrawerType2 {
        id: drawer
        anchors.fill: parent

        onClosed: {
            if (!GC.isMobile()) {
                focusItem.forceActiveFocus()
            }
        }

        collapsedContent: Item {
            implicitHeight: Qt.platform.os !== "ios" ? root.height * 0.9 : screen.height * 0.77
            Component.onCompleted: {
                drawer.expandedHeight = implicitHeight
            }
            Connections {
                target: drawer
                enabled: !GC.isMobile()
                function onActiveFocusChanged() {
                    if (drawer.activeFocus && !drawer.isOpened) {
                        collapsedButtonChevron.forceActiveFocus()
                    }
                }
            }
            ColumnLayout {
                id: collapsed

                anchors.left: parent.left
                anchors.right: parent.right

                Component.onCompleted: {
                    drawer.collapsedHeight = collapsed.implicitHeight
                }

                DividerType {
                    Layout.topMargin: 10
                    Layout.fillWidth: false
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 2
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                }

                RowLayout {
                    Layout.topMargin: 14
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.bottomMargin: 24
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter

                    spacing: 0

                    Connections {
                        target: drawer
                        function onEntered() {
                            if (drawer.isCollapsed) {
                                collapsedButtonChevron.backgroundColor = collapsedButtonChevron.hoveredColor
                                collapsedButtonHeader.opacity = 0.8
                            } else {
                                collapsedButtonHeader.opacity = 1
                            }
                        }

                        function onExited() {
                            if (drawer.isCollapsed) {
                                collapsedButtonChevron.backgroundColor = collapsedButtonChevron.defaultColor
                                collapsedButtonHeader.opacity = 1
                            } else {
                                collapsedButtonHeader.opacity = 1
                            }
                        }

                        function onPressed(pressed, entered) {
                            if (drawer.isCollapsed) {
                                collapsedButtonChevron.backgroundColor = pressed ? collapsedButtonChevron.pressedColor : entered ? collapsedButtonChevron.hoveredColor : collapsedButtonChevron.defaultColor
                                collapsedButtonHeader.opacity = 0.7
                            } else {
                                collapsedButtonHeader.opacity = 1
                            }
                        }
                    }

                    Image {
                        Layout.rightMargin: 8
                        Layout.preferredWidth: 29.4
                        Layout.preferredHeight: 21

                        visible: source !== ""
                        source: RegionsModel.selectedRegionImagePath
                    }

                    Header1TextType {
                        id: collapsedButtonHeader
                        Layout.maximumWidth: drawer.width - 48 - 18 - 12

                        maximumLineCount: 2
                        elide: Qt.ElideRight

                        text: RegionsModel.selectedRegionName
                        horizontalAlignment: Qt.AlignHCenter

                        KeyNavigation.tab: tabBar

                        Behavior on opacity {
                            PropertyAnimation { duration: 200 }
                        }
                    }

                    ImageButtonType {
                        id: collapsedButtonChevron

                        Layout.leftMargin: 8

                        visible: drawer.isCollapsed

                        hoverEnabled: false
                        image: "qrc:/images/controls/chevron-down.svg"
                        imageColor: AmneziaStyle.color.paleGray

                        icon.width: 18
                        icon.height: 18
                        backgroundRadius: 16
                        horizontalPadding: 4
                        topPadding: 4
                        bottomPadding: 3

                        Keys.onEnterPressed: collapsedButtonChevron.clicked()
                        Keys.onReturnPressed: collapsedButtonChevron.clicked()
                        Keys.onTabPressed: lastItemTabClicked()


                        onClicked: {
                            if (drawer.isCollapsed) {
                                drawer.open()
                            }
                        }
                    }
                }
            }

            Connections {
                target: drawer
                enabled: !GC.isMobile()
                function onIsCollapsedChanged() {
                    if (!drawer.isCollapsed) {
                        focusItem1.forceActiveFocus()
                    }
                }
            }

            ColumnLayout {
                id: serversMenuHeader

                anchors.top: collapsed.bottom
                anchors.right: parent.right
                anchors.left: parent.left

                // RowLayout {
                //     Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                //     spacing: 8

                //     visible: true

                //     Item {
                //         id: focusItem1
                //         KeyNavigation.tab: containersDropDown
                //     }

                //     DropDownType {
                //         id: containersDropDown

                //         rootButtonImageColor: AmneziaStyle.color.midnightBlack
                //         rootButtonBackgroundColor: AmneziaStyle.color.paleGray
                //         rootButtonBackgroundHoveredColor: Qt.rgba(215, 216, 219, 0.8)
                //         rootButtonBackgroundPressedColor: Qt.rgba(215, 216, 219, 0.65)
                //         rootButtonHoveredBorderColor: AmneziaStyle.color.transparent
                //         rootButtonDefaultBorderColor: AmneziaStyle.color.transparent
                //         rootButtonTextTopMargin: 8
                //         rootButtonTextBottomMargin: 8

                //         text: RegionsModel.selectedRegionProtocolName
                //         textColor: AmneziaStyle.color.midnightBlack
                //         headerText: qsTr("VPN protocol")
                //         headerBackButtonImage: "qrc:/images/controls/arrow-left.svg"

                //         rootButtonClickedFunction: function() {
                //             containersDropDown.close()
                //         }

                //         drawerParent: root
                //         KeyNavigation.tab: serversMenuContent

                //         listView: HomeContainersListView {
                //             id: containersListView
                //             rootWidth: root.width
                //             onVisibleChanged: {
                //                 if (containersDropDown.visible && !GC.isMobile()) {
                //                     focusItem1.forceActiveFocus()
                //                 }
                //             }

                //             model: SelectedServerProtocolsModel
                //         }
                //     }
                // }

                Header2Type {
                    Layout.fillWidth: true
                    // Layout.topMargin: 48
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16

                    headerText: qsTr("Regions")
                }
            }

            ButtonGroup {
                id: serversRadioButtonGroup
            }

            ListView {
                id: serversMenuContent

                anchors.top: serversMenuHeader.bottom
                anchors.right: parent.right
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.topMargin: 16

                model: RegionsModel
                currentIndex: RegionsModel.selectedRegionIndex

                ScrollBar.vertical: ScrollBar {
                    id: scrollBar
                    policy: serversMenuContent.height >= serversMenuContent.contentHeight ? ScrollBar.AlwaysOff : ScrollBar.AlwaysOn
                }


                activeFocusOnTab: true
                focus: true

                property int focusItemIndex: 0
                onActiveFocusChanged: {
                    if (activeFocus) {
                        serversMenuContent.focusItemIndex = 0
                        serversMenuContent.itemAtIndex(focusItemIndex).forceActiveFocus()
                    }
                }

                onFocusItemIndexChanged: {
                    const focusedElement = serversMenuContent.itemAtIndex(focusItemIndex)
                    if (focusedElement) {
                        if (focusedElement.y + focusedElement.height > serversMenuContent.height) {
                            serversMenuContent.contentY = focusedElement.y + focusedElement.height - serversMenuContent.height
                        } else {
                            serversMenuContent.contentY = 0
                        }
                    }
                }

                Keys.onUpPressed: scrollBar.decrease()
                Keys.onDownPressed: scrollBar.increase()

                Connections {
                    target: drawer
                    enabled: !GC.isMobile()
                    function onIsCollapsedChanged() {
                        if (drawer.isCollapsed) {
                            const item = serversMenuContent.itemAtIndex(serversMenuContent.focusItemIndex)
                            if (item) { item.serverRadioButtonProperty.focus = false }
                        }
                    }
                }

                Connections {
                    target: RegionsModel
                    function onSelectedRegionChanged() {
                        serversMenuContent.currentIndex = RegionsModel.selectedRegionIndex
                    }
                }

                clip: true

                delegate: Item {
                    id: menuContentDelegate

                    property variant delegateData: model
                    property VerticalRadioButton serverRadioButtonProperty: serverRadioButton

                    implicitWidth: serversMenuContent.width
                    implicitHeight: serverRadioButtonContent.implicitHeight

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            serverRadioButton.forceActiveFocus()
                        }
                    }

                    ColumnLayout {
                        id: serverRadioButtonContent

                        anchors.fill: parent
                        anchors.rightMargin: 16
                        anchors.leftMargin: 16

                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            VerticalRadioButton {
                                id: serverRadioButton

                                Layout.fillWidth: true

                                text: regionName

                                preTextImageSource: regionImagePath
                                preTextImageWidth: 21
                                preTextImageHeight: 15
                                showPreTextImage: true

                                checked: index === serversMenuContent.currentIndex
                                checkable: !ConnectionController.isConnected

                                ButtonGroup.group: serversRadioButtonGroup

                                onClicked: {
                                    if (ConnectionController.isConnected) {
                                        PageController.showNotificationMessage(qsTr("Unable to change server while there is an active connection"))
                                        return
                                    }

                                    serversMenuContent.currentIndex = index
                                    RegionsModel.selectedRegionIndex = index
                                }

                                MouseArea {
                                    anchors.fill: serverRadioButton
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: false
                                }

                                Keys.onEnterPressed: serverRadioButton.clicked()
                                Keys.onReturnPressed: serverRadioButton.clicked()
                            }
                        }

                        DividerType {
                            Layout.fillWidth: true
                            Layout.leftMargin: 0
                            Layout.rightMargin: 0
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        AuthController.refreshServers();
    }
}

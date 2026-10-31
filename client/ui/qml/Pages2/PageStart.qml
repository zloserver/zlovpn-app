import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    defaultActiveFocusItem: homeTabButton

    property bool isControlsDisabled: false
    property bool isTabBarDisabled: false

    Connections {
        target: PageController

        function onGoToPageHome() {
            console.log("go to page home")
            if (AuthController.updateRequired) {
                tabBar.visible = false
                tabBarStackView.goToTabBarPage(PageEnum.PageUpdateRequired)
            } else if (FirstSetupController.firstSetupNeeded()) {
                tabBar.visible = false
                tabBarStackView.goToTabBarPage(PageEnum.PageFirstSetup)
            } else if (!AuthController.spikeReady && !AuthController.offlineMode) {
                tabBar.visible = false
                tabBarStackView.goToTabBarPage(PageEnum.PageLookingForServer)
            } else if (!AuthController.hasToken()) {
                tabBar.visible = false
                tabBarStackView.goToTabBarPage(PageEnum.PageSetupWizardStart)
            } else {
                tabBar.visible = true
                tabBar.setCurrentIndex(0)
                tabBarStackView.goToTabBarPage(PageEnum.PageHome)
            }
        }

        function onGoToPageSettings() {
            tabBar.setCurrentIndex(2)
            tabBarStackView.goToTabBarPage(PageEnum.PageSettings)
        }

        function onGoToPageViewConfig() {
            var pagePath = PageController.getPagePath(PageEnum.PageSetupWizardViewConfig)
            tabBarStackView.push(pagePath, { "objectName" : pagePath }, StackView.PushTransition)
        }

        function onDisableControls(disabled) {
            isControlsDisabled = disabled
        }

        function onDisableTabBar(disabled) {
            isTabBarDisabled = disabled
        }

        function onClosePage() {
            if (tabBarStackView.depth <= 1) {
                PageController.hideWindow()
                return
            }
            tabBarStackView.pop()
        }

        function onGoToPage(page, slide) {
            var pagePath = PageController.getPagePath(page)

            if (slide) {
                tabBarStackView.push(pagePath, { "objectName" : pagePath }, StackView.PushTransition)
            } else {
                tabBarStackView.push(pagePath, { "objectName" : pagePath }, StackView.Immediate)
            }
        }

        function onGoToStartPage() {
            while (tabBarStackView.depth > 1) {
                tabBarStackView.pop()
            }
        }

        function onEscapePressed() {
            if (root.isControlsDisabled || root.isTabBarDisabled) {
                return
            }

            var pageName = tabBarStackView.currentItem.objectName
            if ((pageName === PageController.getPagePath(PageEnum.PageShare)) ||
                    (pageName === PageController.getPagePath(PageEnum.PageSettings)) ||
                    (pageName === PageController.getPagePath(PageEnum.PageSetupWizardConfigSource))) {
                PageController.goToPageHome()
            } else {
                PageController.closePage()
            }
        }

        function onForceTabBarActiveFocus() {
            homeTabButton.focus = true
            tabBar.forceActiveFocus()
        }

        function onForceStackActiveFocus() {
            homeTabButton.focus = true
            tabBarStackView.forceActiveFocus()
        }
    }

    Connections {
        target: ConnectionController

        function onReconnectWithUpdatedContainer(message) {
            PageController.showNotificationMessage(message)
            PageController.closePage()
        }

        function onConnectionNoticeOccurred(message) {
            PageController.showNotificationMessage(message)
        }

        function onNoInstalledContainers() {
        }
    }

    Connections {
        target: ImportController

        function onImportErrorOccurred(error, goToPageHome) {
            PageController.showErrorMessage(error)
        }

        function onRestoreAppConfig(data) {
            PageController.showBusyIndicator(true)
            SettingsController.restoreAppConfigFromData(data)
            PageController.showBusyIndicator(false)
        }
    }

    Connections {
        target: SettingsController

        function onLoggingDisableByWatcher() {
            PageController.showNotificationMessage(qsTr("Logging was disabled after 14 days, log files were deleted"))
        }

        function onRestoreBackupFinished() {
            PageController.showNotificationMessage(qsTr("Settings restored from backup file"))
            PageController.goToPageHome()
        }

        function onLoggingStateChanged() {
            if (SettingsController.isLoggingEnabled) {
                var message = qsTr("Logging is enabled. Note that logs will be automatically" +
                                   "disabled after 14 days, and all log files will be deleted.")
                PageController.showNotificationMessage(message)
            }
        }
    }

    Connections {
        target: AuthController

        function onTokenUpdated(authenticationStateChanged) {
            PageController.goToPageHome();
        }

        function onSpikeUpdated() {
            PageController.goToPageHome()
        }

        function onApiCompatibilityChanged() {
            console.log("api compat changed")
            PageController.goToPageHome()
        }

        function onOfflineModeChanged() {
            PageController.goToPageHome()
        }

        function onErrorOccurred(error) {
            if (!error.isFormError) {
                PageController.showErrorMessage(error.errorMessage)
            }
        }
    }

    StackViewType {
        id: tabBarStackView

        anchors.top: parent.top
        anchors.right: parent.right
        anchors.left: parent.left
        anchors.bottom: tabBar.top

        anchors.topMargin: ScreenMargins.margins.top
        anchors.leftMargin: ScreenMargins.margins.left
        anchors.rightMargin: ScreenMargins.margins.right

        enabled: !root.isControlsDisabled

        function goToTabBarPage(page) {
            var pagePath = PageController.getPagePath(page)
            tabBarStackView.clear(StackView.Immediate)
            tabBarStackView.replace(pagePath, { "objectName" : pagePath }, StackView.Immediate)
        }

        Component.onCompleted: {
            var pagePath
            if (AuthController.updateRequired) {
                tabBar.visible = false
                tabBarStackView.goToTabBarPage(PageEnum.PageUpdateRequired)
            } else if (FirstSetupController.firstSetupNeeded()) {
                tabBar.visible = false
                pagePath = PageController.getPagePath(PageEnum.PageFirstSetup)
            } else if (!AuthController.spikeReady && !AuthController.offlineMode) {
                tabBar.visible = false
                pagePath = PageController.getPagePath(PageEnum.PageLookingForServer)
            } else if (!AuthController.hasToken()) {
                tabBar.visible = false
                pagePath = PageController.getPagePath(PageEnum.PageSetupWizardStart)
            } else {
                tabBar.visible = true
                pagePath = PageController.getPagePath(PageEnum.PageHome)
            }

            tabBarStackView.push(pagePath, { "objectName" : pagePath })
        }
    }

    TabBar {
        id: tabBar

        anchors.right: parent.right
        anchors.left: parent.left
        anchors.bottom: parent.bottom

        anchors.bottomMargin: ScreenMargins.margins.bottom

        topPadding: 8
        bottomPadding: 8
        leftPadding: 96
        rightPadding: 96

        height: visible ? homeTabButton.implicitHeight + tabBar.topPadding + tabBar.bottomPadding : 0

        enabled: !root.isControlsDisabled && !root.isTabBarDisabled

        background: Shape {
            width: parent.width
            height: parent.height

            ShapePath {
                startX: 0
                startY: 0

                PathLine { x: width; y: 0 }
                PathLine { x: width; y: height - 1 }
                PathLine { x: 0; y: height - 1 }
                PathLine { x: 0; y: 0 }

                strokeWidth: 1
                strokeColor: AmneziaStyle.color.slateGray
                fillColor: AmneziaStyle.color.onyxBlack
            }
        }

        TabImageButtonType {
            id: homeTabButton
            isSelected: tabBar.currentIndex === 0
            image: "qrc:/images/controls/home.svg"
            clickedFunc: function () {
                tabBarStackView.goToTabBarPage(PageEnum.PageHome)
                // ServersModel.processedIndex = ServersModel.selectedServerIndex
                tabBar.currentIndex = 0
            }

            KeyNavigation.tab: settingsTabButton
            Keys.onEnterPressed: this.clicked()
            Keys.onReturnPressed: this.clicked()
        }

        TabImageButtonType {
            id: settingsTabButton
            isSelected: tabBar.currentIndex === 1
            image: "qrc:/images/controls/settings-2.svg"
            clickedFunc: function () {
                tabBarStackView.goToTabBarPage(PageEnum.PageSettings)
                tabBar.currentIndex = 1
            }

            KeyNavigation.tab: userAccountButton
        }

        TabImageButtonType {
            id: userAccountButton
            isSelected: tabBar.currentIndex === 2
            image: "qrc:/images/controls/user.svg"
            clickedFunc: function( ){
                tabBarStackView.goToTabBarPage(PageEnum.PageUserAccount)
                tabBar.currentIndex = 2
            }

            Keys.onTabPressed: PageController.forceStackActiveFocus()
        }
    }

    Keys.onPressed: function(event) {
        PageController.keyPressEvent(event.key)
        event.accepted = true
    }
}

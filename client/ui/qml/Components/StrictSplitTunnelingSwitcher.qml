import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../Controls2"

// Strict split tunneling (issue #2457): also block apps that bypass app split tunneling by
// binding to the VPN interface directly. Read when the tunnel starts; enforced on the
// AmneziaWG/WireGuard datapath only.
SwitcherType {
    id: root

    visible: Qt.platform.os === "android"
    enabled: AppSplitTunnelingController.isSplitTunnelingEnabled
             && ServersUiController.isDefaultServerDefaultContainerSupportsStrictSplitTunneling

    text: qsTr("Strict split tunneling")
    descriptionText: qsTr("Block apps that split tunneling keeps out of the VPN, even if they connect to the VPN interface directly. Works with AmneziaWG and WireGuard")

    checked: SettingsController.strictSplitTunnelingEnabled

    function restoreBinding() {
        root.checked = Qt.binding(function() { return SettingsController.strictSplitTunnelingEnabled })
    }

    onToggled: function() {
        if (!checked) {
            SettingsController.strictSplitTunnelingEnabled = false
            restoreBinding()
            return
        }

        var headerText = qsTr("Enable strict split tunneling?")
        var descriptionText = qsTr("Some apps detect a VPN by connecting through its interface directly, bypassing the split tunneling rules, and can learn the server's IP address this way. Strict mode drops such connections. It takes effect from the next connection.\n\nIn the \"only the apps from the list\" mode, Private DNS set to a provider hostname stops working for the listed apps. Set Private DNS to Automatic or Off.")
        var yesButtonText = qsTr("Continue")
        var noButtonText = qsTr("Cancel")

        var yesButtonFunction = function() {
            SettingsController.strictSplitTunnelingEnabled = true
            restoreBinding()
        }
        var noButtonFunction = function() {
            restoreBinding()
        }

        showQuestionDrawer(headerText, descriptionText, yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
    }
}

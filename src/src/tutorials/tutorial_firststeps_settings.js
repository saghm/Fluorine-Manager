function getTutorialSteps()
{
    tutorialCanceller.visible = false
    return [
        function() {
            highlightItem("settingsSections", true)
            tutorial.text = qsTr("It is possible to download files directly from Nexus.\n\n"
                               + "Please open the \"Downloads & Nexus\" section.")
            tutorialControl.waitForTabOpen("tabWidget", "nexusTab")
        },

        function() {
            highlightItem("associateButton", false)
            tutorial.text = qsTr("Clicking on this button should register Nexus \"Download with Manager\" buttons "
                                +"to download with Fluorine.")
            waitForClick()
        },

        function() {
            highlightItem("nexusBox", false)
            tutorial.text = qsTr("Use this interface to obtain an API key from NexusMods. "
                                +"This is used for all API connections - downloads, updates "
                                +"etc. Fluorine stores "
                                +"these credentials for your account. If browser sign-in is failing, "
                                +"use the manual entry and copy the API key from your profile.")
            waitForClick()
        }
    ]
}

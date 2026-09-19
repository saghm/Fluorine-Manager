//TL First Steps#0
function getTutorialSteps()
{
  return [
    function() {
        tutorial.text = qsTr("Welcome to the Fluorine Manager Tutorial! This will guide you through the most common "
                           + "features of Fluorine.\n\n"
                           + "It is highly recommended for first-time users to complete the tutorial from beginning "
                           + "to end to properly demonstrate key components of the tool.")
        waitForClick()
    },

    function() {
        tutorial.text = qsTr("Before we continue with the step-by-step tutorial, here are a few ways you can receive "
                           + "help with Fluorine Manager.")
        waitForClick()
    },

    function() {
        tutorial.text = qsTr("View > Notifications displays potential problems detected with your setup and may "
                           + "suggest solutions. (Open Notifications and then close the window to proceed.)")
        if (tutorialControl.waitForAction("actionNotifications")) {
            tutorial.text += qsTr("\n\nIt appears you have one now, however you can hold off on clearing it until after "
                                + "completing the tutorial.")
            highlightAction("actionNotifications", true)
        } else {
            highlightAction("actionNotifications", false)
            waitForClick()
        }
    },

    function() {
        tutorial.text = qsTr("The Help menu contains documentation, support links, and tutorials. "
                           + "Use Help on UI to learn about individual controls.")
        highlightItem("menuBar", false)
        waitForClick()
    },

    function() {
        unhighlight()
        tutorial.text = qsTr("Finally, there are tooltips and extra information available all across Fluorine Manager. If "
                           + "there is a control you don't understand, please try hovering over it for a short "
                           + "description. Alternatively, you can use \"Help on UI\" from the Help menu to click on "
                           + "some controls and get a comprehensive explanation.")
        waitForClick()
    },

    function() {
        tutorial.text = qsTr("This list displays all mods installed through Fluorine. It also displays installed DLCs and "
                           + "any 'unmanaged' mods installed outside Fluorine. You have limited control over those.")
        highlightItem("modList", false)
        waitForClick()
    },

    function() {
        tutorial.text = qsTr("Before we start installing mods, let's have a quick look at the settings. (Open the "
                           + "settings dialog from Tools > Settings to proceed.)")
        manager.activateTutorial("SettingsDialog", "tutorial_firststeps_settings.js")
        if (tutorialControl.waitForAction("actionSettings")) {
            highlightAction("actionSettings", true)
        } else {
          console.error("settings action broken")
          waitForClick()
        }
    },

    function() {
        unhighlight()
        tutorial.text = qsTr("Now it's time to install a mod!\n\n"
                            + "(This will be a requirement in order to demonstrate other features later.)")
        waitForClick()
    },

    function() {
        tutorial.text = qsTr("Use File > Mod Websites to find mods in your browser. "
                           + "If you associated Fluorine with NXM links in Settings, "
                           + "Download with Manager on Nexus sends downloads to Fluorine.")
        highlightItem("menuBar", false)
        waitForClick()
    },

    function() {
        tutorial.text = qsTr("You can also install mods from disk using File > Install Mod from Archive.")
        highlightAction("actionInstallMod", false)
        waitForClick()
    },

    function() {
        tutorial.text = qsTr("Downloads will appear on the \"Downloads\" tab here. You have to download and install at "
                           + "least one mod to proceed.")
        organizer.modInstalled.connect(nextStep)
        highlightItem("tabWidget", true)
    },

    function() {
        unhighlight()
        organizer.modInstalled.disconnect(nextStep)
        tutorial.text = qsTr("Great, you just installed your first mod. Please note that the installation procedure "
                           + "may differ based on how a mod was packaged.")
        waitForClick()
    },

    function() {
        unhighlight()
        tutorial.text = qsTr("Now you know all about downloading and installing mods, but they are not enabled yet...")
        waitForClick()
    },

    function() {
        tutorial.text = qsTr("Install a few more mods if you want, then enable them by checking them in the left pane. "
                             + "Mods that aren't enabled have no effect on the game whatsoever. ")
        highlightItem("modList", true)
        modList.tutorialModlistUpdate.connect(nextStep)
    },

    function() {
        modList.tutorialModlistUpdate.disconnect(nextStep)
        unhighlight()
        tutorial.text = qsTr("For some mods, enabling it on the left pane is all you have to do...")
        waitForClick()
    },

    function() {
        tutorial.text = qsTr("...but for some games they may contain plugins. These are files the game must load and "
                            + "are required to change or add aspects of the game (new weapons, armors, quests, areas, ...).\n\n"
                            + "Please open the \"Plugins\" tab to get a list of plugins.")
        if (tutorialControl.waitForTabOpen("tabWidget", "espTab")) {
            highlightItem("tabWidget", true)
        } else {
            waitForClick()
        }
    },

    function() {
        tutorial.text = qsTr("You may notice some plugins are grayed out. These are part of the main game and can't be "
                            +"disabled.")
        waitForClick()
    },

    function() {
        tutorial.text = qsTr("A single mod may contain zero, one, or multiple plugin files. Some or all may be optional. "
                              + "If in doubt, please consult the documentation of the individual mod. "
                              + "To do so, right-click the mod and select \"Information\".")
        highlightItem("modList", true)
        manager.activateTutorial("ModInfoDialog", "tutorial_firststeps_modinfo.js")
        applicationWindow.modInfoDisplayed.connect(nextStep)
    },

    function() {
        tutorial.text = qsTr("Now you know how to download, install, and enable mods.\n\n"
                           + "It's important you always start the game from inside MO, otherwise "
                           + "the mods you installed here won't work.")
        highlightItem("startGroup", false)
        waitForClick()
    },

    function() {
        tutorial.text = qsTr("This combobox lets you choose <i>what</i> to start. This is how you will launch the game "
                              + "or any other tool which must access the game's mod directories. If a tool is not "
                              + "listed here, you can also configure these options, but that is an advanced topic.")
        highlightItem("executablesListBox", false)
        waitForClick()
    },

    function() {
        tutorial.text = qsTr("This completes the basic tutorial. Feel free to play the game and try out your new mods! "
                           + "Once you have installed a larger number, you may want to continue with the tutorial "
                           + "on conflict resolution.")
        highlightItem("startButton", false)
        waitForClick()
    }
  ]
}

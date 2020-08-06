// As per https://stackoverflow.com/questions/25105269/silent-install-qt-run-installer-on-ubuntu-server
// Info: 
// - https://bugreports.qt.io/browse/QTIFW-166?page=com.atlassian.jira.plugin.system.issuetabpanels%3Acomment-tabpanel&showAll=true
// - https://bugreports.qt.io/browse/QTIFW-892
// - https://bugreports.qt.io/browse/QTIFW-166
// 
// Pre-req: 
//  - Before running make sure to setup a not working system proxy (to trigger the installer credential bypass behavior)
//     or
//    you can also disable the network interface during the installation ...
//
// Run as:
// : qt-opensource-windows-x86-5.14.2.exe --verbose --proxy --script qt-installer-noninteractive.qs
//
// Upon update, the selected package name must be adjusted, to find the new names, simply comments the "nextbutton" action in ComponentSelectionPageCallback and launch it manually and look for the names to appears in the log window...
// 

function Controller() {
    installer.autoRejectMessageBoxes();
    installer.installationFinished.connect(function() {
        gui.clickButton(buttons.NextButton);
    })
}

Controller.prototype.WelcomePageCallback = function() {
    //console.log('FGETest');
    
    //widget = gui.currentPageWidget(); // get the current wizard page
    //console.log(JSON.stringify(widget));
    //gui.clickButton(buttons.CustomButton1); // This open the settings pannel

    gui.clickButton(buttons.NextButton, 360000); // Use 6 minutes, because there is a built-in timeout that continue with "Proceed without Qt Account login"
}

Controller.prototype.CredentialsPageCallback = function() {
    gui.clickButton(buttons.NextButton);
}

Controller.prototype.IntroductionPageCallback = function() {
    gui.clickButton(buttons.NextButton);
}

Controller.prototype.TargetDirectoryPageCallback = function()
{
    // Keep the default installation dir
    // gui.currentPageWidget().TargetDirectoryLineEdit.setText(installer.value("HomeDir") + "/Qt");
    gui.clickButton(buttons.NextButton);
}

Controller.prototype.ComponentSelectionPageCallback = function() {
    var widget = gui.currentPageWidget();

    widget.deselectAll();
    widget.selectComponent("qt.qt5.5142.win64_msvc2017_64");
    widget.selectComponent("qt.qt5.5142.qtwebengine");

    gui.clickButton(buttons.NextButton);
}

Controller.prototype.LicenseAgreementPageCallback = function() {
    gui.currentPageWidget().AcceptLicenseRadioButton.setChecked(true);
    gui.clickButton(buttons.NextButton);
}

Controller.prototype.StartMenuDirectoryPageCallback = function() {
    gui.clickButton(buttons.NextButton);
}

Controller.prototype.ReadyForInstallationPageCallback = function()
{
    gui.clickButton(buttons.NextButton);
}

Controller.prototype.FinishedPageCallback = function() {
    var checkBoxForm = gui.currentPageWidget().LaunchQtCreatorCheckBoxForm
    if (checkBoxForm && checkBoxForm.launchQtCreatorCheckBox) {
        checkBoxForm.launchQtCreatorCheckBox.checked = false;
    }
    gui.clickButton(buttons.FinishButton);
}

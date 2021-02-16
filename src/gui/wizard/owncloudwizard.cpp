/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
 * Copyright (C) by Krzesimir Nowak <krzesimir@endocode.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "account.h"
#include "config.h"
#include "configfile.h"
#include "theme.h"
#include "owncloudgui.h"

#include "wizard/owncloudwizard.h"
#include "wizard/welcomepage.h"
#include "wizard/owncloudsetuppage.h"
#include "wizard/owncloudhttpcredspage.h"
#include "wizard/owncloudoauthcredspage.h"
#include "wizard/owncloudadvancedsetuppage.h"
#include "wizard/owncloudwizardresultpage.h"
#include "wizard/webviewpage.h"
#include "wizard/flow2authcredspage.h"

#include "common/vfs.h"

#include "QProgressIndicator.h"

#include <QtCore>
#include <QtGui>
#include <QMessageBox>
#include <owncloudgui.h>

#include <cstdlib>

namespace OCC {

Q_LOGGING_CATEGORY(lcWizard, "nextcloud.gui.wizard", QtInfoMsg)

OwncloudWizard::OwncloudWizard(QWidget *parent)
    : QWizard(parent)
    , _account(nullptr)
#ifdef WITH_PROVIDERS
    , _welcomePage(new WelcomePage(this))
#endif // WITH_PROVIDERS
    , _setupPage(new OwncloudSetupPage(this))
    , _httpCredsPage(new OwncloudHttpCredsPage(this))
    , _browserCredsPage(new OwncloudOAuthCredsPage)
    , _flow2CredsPage(new Flow2AuthCredsPage)
    , _advancedSetupPage(new OwncloudAdvancedSetupPage(this))
    , _resultPage(new OwncloudWizardResultPage)
    , _webViewPage(new WebViewPage(this))
{
    setObjectName("owncloudWizard");

    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
#ifdef WITH_PROVIDERS
    setPage(WizardCommon::Page_Welcome, _welcomePage);
#endif // WITH_PROVIDERS
    setPage(WizardCommon::Page_ServerSetup, _setupPage);
    setPage(WizardCommon::Page_HttpCreds, _httpCredsPage);
    setPage(WizardCommon::Page_OAuthCreds, _browserCredsPage);
    setPage(WizardCommon::Page_Flow2AuthCreds, _flow2CredsPage);
    setPage(WizardCommon::Page_AdvancedSetup, _advancedSetupPage);
    setPage(WizardCommon::Page_Result, _resultPage);
    setPage(WizardCommon::Page_WebView, _webViewPage);

    connect(this, &QDialog::finished, this, &OwncloudWizard::basicSetupFinished);

    // note: start Id is set by the calling class depending on if the
    // welcome text is to be shown or not.

    connect(this, &QWizard::currentIdChanged, this, &OwncloudWizard::slotCurrentPageChanged);
    connect(_setupPage, &OwncloudSetupPage::determineAuthType, this, &OwncloudWizard::determineAuthType);
    connect(_httpCredsPage, &OwncloudHttpCredsPage::connectToOCUrl, this, &OwncloudWizard::connectToOCUrl);
    connect(_browserCredsPage, &OwncloudOAuthCredsPage::connectToOCUrl, this, &OwncloudWizard::connectToOCUrl);
    connect(_flow2CredsPage, &Flow2AuthCredsPage::connectToOCUrl, this, &OwncloudWizard::connectToOCUrl);
    connect(_webViewPage, &WebViewPage::connectToOCUrl, this, &OwncloudWizard::connectToOCUrl);
    connect(_advancedSetupPage, &OwncloudAdvancedSetupPage::createLocalAndRemoteFolders,
        this, &OwncloudWizard::createLocalAndRemoteFolders);
    connect(this, &QWizard::customButtonClicked, this, &OwncloudWizard::skipFolderConfiguration);


    Theme *theme = Theme::instance();
    setWindowTitle(tr("Add %1 account").arg(theme->appNameGUI()));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage);
    setOption(QWizard::NoBackButtonOnLastPage);
    setOption(QWizard::NoCancelButton);
    setButtonText(QWizard::CustomButton1, tr("Skip folders configuration"));

    // Change the next buttons size policy since we hide it on the
    // welcome page but want it to fill it's space that we don't get
    // flickering when the page changes
    auto nextButtonSizePolicy = button(QWizard::NextButton)->sizePolicy();
    nextButtonSizePolicy.setRetainSizeWhenHidden(true);
    button(QWizard::NextButton)->setSizePolicy(nextButtonSizePolicy);

    // Connect styleChanged events to our widgets, so they can adapt (Dark-/Light-Mode switching)
    connect(this, &OwncloudWizard::styleChanged, _setupPage, &OwncloudSetupPage::slotStyleChanged);
    connect(this, &OwncloudWizard::styleChanged, _advancedSetupPage, &OwncloudAdvancedSetupPage::slotStyleChanged);
    connect(this, &OwncloudWizard::styleChanged, _flow2CredsPage, &Flow2AuthCredsPage::slotStyleChanged);

    customizeStyle();

    // allow Flow2 page to poll on window activation
    connect(this, &OwncloudWizard::onActivate, _flow2CredsPage, &Flow2AuthCredsPage::slotPollNow);

    adjustWizardSize();
}

void OwncloudWizard::adjustWizardSize()
{
    QList<QSize> pageSizes;
    const auto pIds = pageIds();
    std::transform(pIds.begin(), pIds.end(), std::back_inserter(pageSizes), [this](int pageId) {
        auto p = page(pageId);

        p->adjustSize();

        return p->sizeHint();
    });

    const auto maxPageSizeIter = std::max_element(pageSizes.begin(), pageSizes.end(), [](const QSize &size1, const QSize &size2) {
        const auto size1Widest = size1.width() > size1.height() ? size1.width() : size1.height();
        const auto size2Widest = size2.width() > size2.height() ? size2.width() : size2.height();

        if (size1Widest < size2Widest) {
            return true;
        }

        return false;
    });

    Q_ASSERT(maxPageSizeIter != pageSizes.end());
    const auto maxPageSizeWidest = maxPageSizeIter->width() > maxPageSizeIter->height() ? maxPageSizeIter->width() : maxPageSizeIter->height();
    const QSize newWizardSize(maxPageSizeWidest, maxPageSizeWidest);
    resize(newWizardSize);
}

void OwncloudWizard::setAccount(AccountPtr account)
{
    _account = account;
}

AccountPtr OwncloudWizard::account() const
{
    return _account;
}

QString OwncloudWizard::localFolder() const
{
    return (_advancedSetupPage->localFolder());
}

QStringList OwncloudWizard::selectiveSyncBlacklist() const
{
    return _advancedSetupPage->selectiveSyncBlacklist();
}

bool OwncloudWizard::useVirtualFileSync() const
{
    return _advancedSetupPage->useVirtualFileSync();
}

bool OwncloudWizard::isConfirmBigFolderChecked() const
{
    return _advancedSetupPage->isConfirmBigFolderChecked();
}

QString OwncloudWizard::ocUrl() const
{
    QString url = field("OCUrl").toString().simplified();
    return url;
}

bool OwncloudWizard::registration()
{
    return _registration;
}

void OwncloudWizard::setRegistration(bool registration)
{
    _registration = registration;
}


void OwncloudWizard::enableFinishOnResultWidget(bool enable)
{
    _resultPage->setComplete(enable);
}

void OwncloudWizard::setRemoteFolder(const QString &remoteFolder)
{
    _advancedSetupPage->setRemoteFolder(remoteFolder);
    _resultPage->setRemoteFolder(remoteFolder);
}

void OwncloudWizard::successfulStep()
{
    const int id(currentId());

    switch (id) {
    case WizardCommon::Page_HttpCreds:
        _httpCredsPage->setConnected();
        break;

    case WizardCommon::Page_OAuthCreds:
        _browserCredsPage->setConnected();
        break;

    case WizardCommon::Page_Flow2AuthCreds:
        _flow2CredsPage->setConnected();
        break;

    case WizardCommon::Page_WebView:
        _webViewPage->setConnected();
        break;

    case WizardCommon::Page_AdvancedSetup:
        _advancedSetupPage->directoriesCreated();
        break;

    case WizardCommon::Page_ServerSetup:
    case WizardCommon::Page_Result:
        qCWarning(lcWizard, "Should not happen at this stage.");
        break;
    }

    ownCloudGui::raiseDialog(this);
    next();
}

void OwncloudWizard::setAuthType(DetermineAuthTypeJob::AuthType type)
{
    _setupPage->setAuthType(type);

    if (type == DetermineAuthTypeJob::OAuth) {
        _credentialsPage = _browserCredsPage;
    } else if (type == DetermineAuthTypeJob::LoginFlowV2) {
        _credentialsPage = _flow2CredsPage;
    } else if (type == DetermineAuthTypeJob::WebViewFlow) {
        _credentialsPage = _webViewPage;
    } else { // try Basic auth even for "Unknown"
        _credentialsPage = _httpCredsPage;
    }
    next();
}

// TODO: update this function
void OwncloudWizard::slotCurrentPageChanged(int id)
{
    qCDebug(lcWizard) << "Current Wizard page changed to " << id;

    const auto setNextButtonAsDefault = [this]() {
        auto nextButton = qobject_cast<QPushButton *>(button(QWizard::NextButton));
        if (nextButton) {
            nextButton->setDefault(true);
        }
    };

    if (id == WizardCommon::Page_Welcome) {
        // Set next button to just hidden so it retains it's layout
        button(QWizard::NextButton)->setHidden(true);
        // Need to set it from here, otherwise it has no effect
        _welcomePage->setLoginButtonDefault();
    } else if (id == WizardCommon::Page_WebView || id == WizardCommon::Page_Flow2AuthCreds) {
        setButtonLayout({ QWizard::Stretch, QWizard::BackButton });
    } else if (id == WizardCommon::Page_AdvancedSetup) {
        setButtonLayout({ QWizard::Stretch, QWizard::CustomButton1, QWizard::BackButton, QWizard::NextButton });
        setNextButtonAsDefault();
    } else {
        setButtonLayout({ QWizard::Stretch, QWizard::BackButton, QWizard::NextButton });
        setNextButtonAsDefault();
    }

    if (id == WizardCommon::Page_ServerSetup) {
        emit clearPendingRequests();
    }

    if (id == WizardCommon::Page_Result) {
        disconnect(this, &QDialog::finished, this, &OwncloudWizard::basicSetupFinished);
        emit basicSetupFinished(QDialog::Accepted);
        appendToConfigurationLog(QString());
        // Immediately close on show, we currently don't want this page anymore
        done(Accepted);
    }

    if (id == WizardCommon::Page_AdvancedSetup && (_credentialsPage == _browserCredsPage || _credentialsPage == _flow2CredsPage)) {
        // For OAuth, disable the back button in the Page_AdvancedSetup because we don't want
        // to re-open the browser.
        button(QWizard::BackButton)->setEnabled(false);
    }
}

void OwncloudWizard::displayError(const QString &msg, bool retryHTTPonly)
{
    switch (currentId()) {
    case WizardCommon::Page_ServerSetup:
        _setupPage->setErrorString(msg, retryHTTPonly);
        break;

    case WizardCommon::Page_HttpCreds:
        _httpCredsPage->setErrorString(msg);
        break;

    case WizardCommon::Page_AdvancedSetup:
        _advancedSetupPage->setErrorString(msg);
        break;
    }
}

void OwncloudWizard::appendToConfigurationLog(const QString &msg, LogType /*type*/)
{
    _setupLog << msg;
    qCDebug(lcWizard) << "Setup-Log: " << msg;
}

void OwncloudWizard::setOCUrl(const QString &url)
{
    _setupPage->setServerUrl(url);
}

AbstractCredentials *OwncloudWizard::getCredentials() const
{
    if (_credentialsPage) {
        return _credentialsPage->getCredentials();
    }

    return nullptr;
}

void OwncloudWizard::changeEvent(QEvent *e)
{
    switch (e->type()) {
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
    case QEvent::ThemeChange:
        customizeStyle();

        // Notify the other widgets (Dark-/Light-Mode switching)
        emit styleChanged();
        break;
    case QEvent::ActivationChange:
        if (isActiveWindow())
            emit onActivate();
        break;
    default:
        break;
    }

    QWizard::changeEvent(e);
}

void OwncloudWizard::customizeStyle()
{
    // HINT: Customize wizard's own style here, if necessary in the future (Dark-/Light-Mode switching)
    auto theme = Theme::instance();
    auto wizardPalette = palette();

    wizardPalette.setColor(QPalette::Window, theme->wizardHeaderBackgroundColor());
    QColor colorWindowDisabled(wizardPalette.color(QPalette::Window));
    colorWindowDisabled.setAlpha(128);
    wizardPalette.setColor(QPalette::Disabled, QPalette::Window, colorWindowDisabled);

    wizardPalette.setColor(QPalette::Base, theme->wizardHeaderBackgroundColor());
    QColor colorBaseDisabled(wizardPalette.color(QPalette::Base));
    colorBaseDisabled.setAlpha(128);
    wizardPalette.setColor(QPalette::Disabled, QPalette::Base, colorBaseDisabled);

    wizardPalette.setColor(QPalette::WindowText, theme->wizardHeaderTitleColor());
    QColor colorWindowTextDisabled(wizardPalette.color(QPalette::WindowText));
    colorWindowTextDisabled.setAlpha(128);
    wizardPalette.setColor(QPalette::Disabled, QPalette::WindowText, colorWindowTextDisabled);

    wizardPalette.setColor(QPalette::Text, theme->wizardHeaderTitleColor());
    QColor colorTextDisabled(wizardPalette.color(QPalette::Text));
    colorTextDisabled.setAlpha(128);
    wizardPalette.setColor(QPalette::Disabled, QPalette::Text, colorWindowTextDisabled);

    // Set separator color
    wizardPalette.setColor(QPalette::Mid, theme->wizardHeaderBackgroundColor());

    setPalette(wizardPalette);

    WizardCommon::customizePrimaryButtonStyle(button(QWizard::NextButton));
    WizardCommon::customizeSecondaryButtonStyle(button(QWizard::CustomButton1));
    WizardCommon::customizeSecondaryButtonStyle(button(QWizard::BackButton));
}

void OwncloudWizard::bringToTop()
{
    // bring wizard to top
    ownCloudGui::raiseDialog(this);
}

void OwncloudWizard::askExperimentalVirtualFilesFeature(QWidget *receiver, const std::function<void(bool enable)> &callback)
{
    const auto bestVfsMode = bestAvailableVfsMode();
    QMessageBox *msgBox = nullptr;
    QPushButton *acceptButton = nullptr;
    switch (bestVfsMode) {
    case Vfs::WindowsCfApi:
        callback(true);
        return;
    case Vfs::WithSuffix:
        msgBox = new QMessageBox(
            QMessageBox::Warning,
            tr("Enable experimental feature?"),
            tr("When the \"virtual files\" mode is enabled no files will be downloaded initially. "
               "Instead, a tiny \"%1\" file will be created for each file that exists on the server. "
               "The contents can be downloaded by running these files or by using their context menu."
               "\n\n"
               "The virtual files mode is mutually exclusive with selective sync. "
               "Currently unselected folders will be translated to online-only folders "
               "and your selective sync settings will be reset."
               "\n\n"
               "Switching to this mode will abort any currently running synchronization."
               "\n\n"
               "This is a new, experimental mode. If you decide to use it, please report any "
               "issues that come up.")
                .arg(APPLICATION_DOTVIRTUALFILE_SUFFIX),
            QMessageBox::NoButton, receiver);
        acceptButton = msgBox->addButton(tr("Enable experimental placeholder mode"), QMessageBox::AcceptRole);
        msgBox->addButton(tr("Stay safe"), QMessageBox::RejectRole);
        break;
    case Vfs::Off:
        Q_UNREACHABLE();
    }

    connect(msgBox, &QMessageBox::accepted, receiver, [callback, msgBox, acceptButton] {
        callback(msgBox->clickedButton() == acceptButton);
        msgBox->deleteLater();
    });
    msgBox->open();
}

} // end namespace

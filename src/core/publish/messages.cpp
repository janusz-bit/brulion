#include "publish/messages.h"

using namespace Qt::StringLiterals;

namespace brulion::publish {

namespace {

QString withDetail(const QString &text, const QString &detail)
{
    const QString d = detail.trimmed();
    return d.isEmpty() ? text : PublishMessages::tr("%1 (%2)").arg(text, d);
}

} // namespace

QString errorText(const QString &code, const QString &detail)
{
    if (code == u"auth")
        return PublishMessages::tr(
                "The server did not accept the sign-in: check the user name and the password or token.");
    if (code == u"forbidden")
        return withDetail(PublishMessages::tr("The server refused it: this account may not write there."), detail);
    if (code == u"no-push")
        return PublishMessages::tr(
                "This GitHub token may not push to the repository: use a token with the “repo” scope or a "
                "repository of your own.");
    if (code == u"archived")
        return PublishMessages::tr("The repository is archived on GitHub: it takes no new commits.");
    if (code == u"not-found")
        return PublishMessages::tr(
                "Not found: check the address (a private repository needs a token that can see it).");
    if (code == u"rate-limited")
        return PublishMessages::tr("GitHub's request limit is used up for now: publish again in a while.");
    if (code == u"not-fast-forward")
        return PublishMessages::tr("The branch kept changing on GitHub while publishing: publish again.");
    if (code == u"server")
        return withDetail(PublishMessages::tr("The server had a problem: try again later."), detail);
    if (code == u"network")
        return PublishMessages::tr("No connection to the server: check the internet connection.");
    if (code == u"timeout")
        return PublishMessages::tr("The server did not answer in time: try again.");
    if (code == u"too-large")
        return withDetail(PublishMessages::tr("Too large for the server."), detail);
    if (code == u"cancelled")
        return PublishMessages::tr("Stopped.");
    if (code == u"parse")
        return PublishMessages::tr("The server's answer was not understood: is it the right address?");
    if (code == u"conflicts")
        return PublishMessages::tr(
                "Some files changed at the target since the last publish (or were there before): nothing was "
                "replaced. Look at them, then choose “Replace them”.");
    if (code == u"no-token")
        return PublishMessages::tr("Publishing to GitHub needs a token: add one in the GitHub tab of the sync center.");
    if (code == u"no-password")
        return PublishMessages::tr("The password of this target is missing: edit the target and type it again.");
    if (code == u"io")
        return withDetail(PublishMessages::tr("The folder could not be written."), detail);
    if (code == u"files-failed")
        return PublishMessages::tr("Some files could not be published: publish again to retry them.");
    if (code == u"share")
        return withDetail(
                PublishMessages::tr("The files are on Nextcloud, but its public link could not be made or changed."),
                detail);
    if (code == u"no-space")
        return PublishMessages::tr("The server has no space left.");
    if (code == u"refused")
        return withDetail(PublishMessages::tr("The server refused it."), detail);
    if (code == u"nothing")
        return PublishMessages::tr("Nothing to publish: add a page, a folder or a tag first.");
    if (code == u"no-name")
        return PublishMessages::tr("Give the target a name.");
    if (code == u"bad-repository")
        return PublishMessages::tr("The repository is written as owner/name, e.g. jan/notatki.");
    if (code == u"bad-branch")
        return PublishMessages::tr("That is no branch name Git accepts.");
    if (code == u"bad-folder")
        return PublishMessages::tr("That is no folder name the target accepts.");
    if (code == u"bad-url")
        return PublishMessages::tr("Give a full address starting with https://.");
    if (code == u"bad-server")
        return PublishMessages::tr("Give the Nextcloud server's address, e.g. cloud.example.com.");
    if (code == u"no-username")
        return PublishMessages::tr("Give the user name.");
    if (code == u"no-folder")
        return PublishMessages::tr("Choose a folder for the site: a public link to the whole account is never made.");
    if (code == u"no-path")
        return PublishMessages::tr("Choose a folder on this device.");
    return withDetail(PublishMessages::tr("Publishing did not work."), detail.isEmpty() ? code : detail);
}

QString warningText(const QString &code, const QString &detail)
{
    if (code == u"moved")
        return PublishMessages::tr("The target's place changed: what was published at the old place stays there.");
    if (code == u"pages-elsewhere")
        return withDetail(
                PublishMessages::tr(
                        "GitHub Pages of this repository is served from another branch or folder: change it in "
                        "the repository's settings to show this site."),
                detail);
    if (code == u"pages-unknown")
        return withDetail(PublishMessages::tr("Whether GitHub Pages is on could not be checked."), detail);
    if (code == u"pages-folder")
        return PublishMessages::tr(
                "GitHub Pages serves only a branch's root or its docs folder: this folder is published, but Pages "
                "was not turned on for it.");
    if (code == u"pages-not-enabled")
        return withDetail(
                PublishMessages::tr(
                        "GitHub Pages could not be turned on: turn it on in the repository's settings (Pages)."),
                detail);
    if (code == u"no-share-password")
        return PublishMessages::tr("The link's password is missing on this device: the link was left as it is.");
    return detail.isEmpty() ? code : detail;
}

QString reasonText(LeftOutReason reason)
{
    switch (reason) {
    case LeftOutReason::Hidden: return PublishMessages::tr("In a hidden folder");
    case LeftOutReason::Private: return PublishMessages::tr("Marked publish: false");
    case LeftOutReason::NotMarked: return PublishMessages::tr("Not marked publish: true");
    case LeftOutReason::Extension: return PublishMessages::tr("An extension's code");
    case LeftOutReason::Conversation: return PublishMessages::tr("A conversation with AI");
    case LeftOutReason::Template: return PublishMessages::tr("A template");
    case LeftOutReason::Encrypted: return PublishMessages::tr("Encrypted");
    case LeftOutReason::ConflictCopy: return PublishMessages::tr("A conflict copy");
    }
    return {};
}

QString actionText(FileAction action, bool deletion)
{
    switch (action) {
    case FileAction::Upload: return PublishMessages::tr("New");
    case FileAction::Update: return PublishMessages::tr("Changed");
    case FileAction::Unchanged: return PublishMessages::tr("Unchanged");
    case FileAction::Delete: return PublishMessages::tr("Removed");
    case FileAction::Conflict:
        return deletion ? PublishMessages::tr("Changed there – kept")
                        : PublishMessages::tr("Changed there – not replaced");
    }
    return {};
}

QString kindText(TargetKind kind)
{
    switch (kind) {
    case TargetKind::GitHubPages: return PublishMessages::tr("GitHub Pages");
    case TargetKind::WebDav: return PublishMessages::tr("WebDAV server");
    case TargetKind::Nextcloud: return PublishMessages::tr("Nextcloud");
    case TargetKind::Folder: return PublishMessages::tr("Folder on this device");
    }
    return {};
}

QString publishedFooter()
{
    return PublishMessages::tr("Published with Brulion");
}

} // namespace brulion::publish

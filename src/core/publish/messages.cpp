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
    using M = PublishMessages;
    if (code == u"auth")
        return M::tr("The server did not accept the sign-in: check the user name and the password or token.");
    if (code == u"forbidden")
        return withDetail(M::tr("The server refused it: this account may not write there."), detail);
    if (code == u"no-push")
        return M::tr("This GitHub token may not push to the repository: use a token with the “repo” scope or a "
                     "repository of your own.");
    if (code == u"archived")
        return M::tr("The repository is archived on GitHub: it takes no new commits.");
    if (code == u"not-found")
        return M::tr("Not found: check the address (a private repository needs a token that can see it).");
    if (code == u"rate-limited")
        return M::tr("GitHub's request limit is used up for now: publish again in a while.");
    if (code == u"not-fast-forward")
        return M::tr("The branch kept changing on GitHub while publishing: publish again.");
    if (code == u"server")
        return withDetail(M::tr("The server had a problem: try again later."), detail);
    if (code == u"network")
        return M::tr("No connection to the server: check the internet connection.");
    if (code == u"timeout")
        return M::tr("The server did not answer in time: try again.");
    if (code == u"too-large")
        return withDetail(M::tr("Too large for the server."), detail);
    if (code == u"cancelled")
        return M::tr("Stopped.");
    if (code == u"parse")
        return M::tr("The server's answer was not understood: is it the right address?");
    if (code == u"conflicts")
        return M::tr("Some files changed at the target since the last publish (or were there before): nothing was "
                     "replaced. Look at them, then choose “Replace them”.");
    if (code == u"no-token")
        return M::tr("Publishing to GitHub needs a token: add one in the GitHub tab of the sync center.");
    if (code == u"no-password")
        return M::tr("The password of this target is missing: edit the target and type it again.");
    if (code == u"io")
        return withDetail(M::tr("The folder could not be written."), detail);
    if (code == u"files-failed")
        return M::tr("Some files could not be published: publish again to retry them.");
    if (code == u"share")
        return withDetail(M::tr("The files are on Nextcloud, but its public link could not be made or changed."),
                          detail);
    if (code == u"no-space")
        return M::tr("The server has no space left.");
    if (code == u"refused")
        return withDetail(M::tr("The server refused it."), detail);
    if (code == u"nothing")
        return M::tr("Nothing to publish: add a page, a folder or a tag first.");
    if (code == u"no-name")
        return M::tr("Give the target a name.");
    if (code == u"bad-repository")
        return M::tr("The repository is written as owner/name, e.g. jan/notatki.");
    if (code == u"bad-branch")
        return M::tr("That is no branch name Git accepts.");
    if (code == u"bad-folder")
        return M::tr("That is no folder name the target accepts.");
    if (code == u"bad-url")
        return M::tr("Give a full address starting with https://.");
    if (code == u"bad-server")
        return M::tr("Give the Nextcloud server's address, e.g. cloud.example.com.");
    if (code == u"no-username")
        return M::tr("Give the user name.");
    if (code == u"no-folder")
        return M::tr("Choose a folder for the site: a public link to the whole account is never made.");
    if (code == u"no-path")
        return M::tr("Choose a folder on this device.");
    return withDetail(M::tr("Publishing did not work."), detail.isEmpty() ? code : detail);
}

QString warningText(const QString &code, const QString &detail)
{
    using M = PublishMessages;
    if (code == u"moved")
        return M::tr("The target's place changed: what was published at the old place stays there.");
    if (code == u"pages-elsewhere")
        return withDetail(M::tr("GitHub Pages of this repository is served from another branch or folder: change it in "
                                "the repository's settings to show this site."),
                          detail);
    if (code == u"pages-unknown")
        return withDetail(M::tr("Whether GitHub Pages is on could not be checked."), detail);
    if (code == u"pages-folder")
        return M::tr("GitHub Pages serves only a branch's root or its docs folder: this folder is published, but Pages "
                     "was not turned on for it.");
    if (code == u"pages-not-enabled")
        return withDetail(
                M::tr("GitHub Pages could not be turned on: turn it on in the repository's settings (Pages)."), detail);
    if (code == u"no-share-password")
        return M::tr("The link's password is missing on this device: the link was left as it is.");
    return detail.isEmpty() ? code : detail;
}

QString reasonText(LeftOutReason reason)
{
    using M = PublishMessages;
    switch (reason) {
    case LeftOutReason::Hidden: return M::tr("In a hidden folder");
    case LeftOutReason::Private: return M::tr("Marked publish: false");
    case LeftOutReason::NotMarked: return M::tr("Not marked publish: true");
    case LeftOutReason::Extension: return M::tr("An extension's code");
    case LeftOutReason::Conversation: return M::tr("A conversation with AI");
    case LeftOutReason::Template: return M::tr("A template");
    case LeftOutReason::Encrypted: return M::tr("Encrypted");
    case LeftOutReason::ConflictCopy: return M::tr("A conflict copy");
    }
    return {};
}

QString actionText(FileAction action, bool deletion)
{
    using M = PublishMessages;
    switch (action) {
    case FileAction::Upload: return M::tr("New");
    case FileAction::Update: return M::tr("Changed");
    case FileAction::Unchanged: return M::tr("Unchanged");
    case FileAction::Delete: return M::tr("Removed");
    case FileAction::Conflict: return deletion ? M::tr("Changed there – kept") : M::tr("Changed there – not replaced");
    }
    return {};
}

QString kindText(TargetKind kind)
{
    using M = PublishMessages;
    switch (kind) {
    case TargetKind::GitHubPages: return M::tr("GitHub Pages");
    case TargetKind::WebDav: return M::tr("WebDAV server");
    case TargetKind::Nextcloud: return M::tr("Nextcloud");
    case TargetKind::Folder: return M::tr("Folder on this device");
    }
    return {};
}

QString publishedFooter()
{
    return PublishMessages::tr("Published with Brulion");
}

} // namespace brulion::publish

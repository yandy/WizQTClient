#include "wizDatabase.h"

#include <QDir>
#include <QUrl>
#include <QDebug>

#include <algorithm>

#include "wizhtml2zip.h"

#include "zip/wizzip.h"


//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//class CWizDocument


CString GetResoucePathFromFile(const CString& strHtmlFileName)
{
    if (!PathFileExists(strHtmlFileName))
        return CString();
    //
    CString strTitle = WizExtractFileTitle(strHtmlFileName);
    //
    CString strPath = ::WizExtractFilePath(strHtmlFileName);
    CString strPath1 = strPath + strTitle + "_files/";
    CString strPath2 = strPath + strTitle + ".files/";
    if (PathFileExists(strPath1))
        return strPath1;
    if (PathFileExists(strPath2))
        return strPath2;
    return CString();
}

CWizDocument::CWizDocument(CWizDatabase& db, const WIZDOCUMENTDATA& data)
    : m_db(db)
    , m_data(data)
{
}

bool CWizDocument::UpdateDocument4(const QString& strHtml, const QString& strURL, int nFlags)
{
    return m_db.UpdateDocumentData(m_data, strHtml, strURL, nFlags);
}

bool CWizDocument::IsInDeletedItemsFolder()
{
    QString strDeletedItemsFolderLocation = m_db.GetDeletedItemsLocation();

    return m_data.strLocation.startsWith(strDeletedItemsFolderLocation);
}

void CWizDocument::PermanentlyDelete()
{
    CWizDocumentAttachmentDataArray arrayAttachment;
    m_db.GetDocumentAttachments(m_data.strGUID, arrayAttachment);

    CWizDocumentAttachmentDataArray::const_iterator it;
    for (it = arrayAttachment.begin(); it != arrayAttachment.end(); it++) {
        CString strFileName = m_db.GetAttachmentFileName(it->strGUID);
        ::WizDeleteFile(strFileName);

        m_db.DeleteAttachment(*it, true);
    }

    if (!m_db.DeleteDocument(m_data, true)) {
        TOLOG1(_T("Failed to delete document: %1"), m_data.strTitle);
        return;
    }

    CString strZipFileName = m_db.GetDocumentFileName(m_data.strGUID);
    if (PathFileExists(strZipFileName))
    {
        WizDeleteFile(strZipFileName);
    }
}

void CWizDocument::MoveTo(QObject* p)
{
    CWizFolder* folder = dynamic_cast<CWizFolder*>(p);
    if (!folder)
        return;

    MoveDocument(folder);
}

bool CWizDocument::MoveDocument(CWizFolder* pFolder)
{
    if (!pFolder)
        return false;

    QString strNewLocation = pFolder->Location();
    QString strOldLocation = m_data.strLocation;

    if (strNewLocation == strOldLocation)
        return true;

    m_data.strLocation = strNewLocation;
    if (!m_db.ModifyDocumentInfo(m_data))
    {
        m_data.strLocation = strOldLocation;
        TOLOG1(_T("Failed to modify document location %1."), m_data.strLocation);
        return false;
    }

    return true;
}

bool CWizDocument::AddTag(const WIZTAGDATA& dataTag)
{
    CWizStdStringArray arrayTag;
    m_db.GetDocumentTags(m_data.strGUID, arrayTag);

    if (-1 != WizFindInArray(arrayTag, dataTag.strGUID))
        return true;

    if (!m_db.InsertDocumentTag(m_data, dataTag.strGUID)) {
        TOLOG1(_T("Failed to insert document tag: %1"), m_data.strTitle);
        return false;
    }

    return true;
}

bool CWizDocument::RemoveTag(const WIZTAGDATA& dataTag)
{
    return m_db.DeleteDocumentTag(m_data, dataTag.strGUID);
}

void CWizDocument::Delete()
{
    if (IsInDeletedItemsFolder()) {
        return PermanentlyDelete();
    } else {
        return MoveTo(m_db.GetDeletedItemsFolder());
    }

}



static const char* g_lpszZiwMeta = "\
<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<META>\n\
    <CLASS>ziw file</CLASS>\n\
    <VERSION>1.0</VERSION>\n\
    <DESCRIPTION>Wiz file formate</DESCRIPTION>\n\
    <TITLE><![CDATA[%title%]]></TITLE>\n\
    <URL><![CDATA[%url%]]></URL>\n\
    <TAGS><![CDATA[%tags%]]></TAGS>\n\
</META>\n\
";

inline CString WizGetZiwMetaText(const CString& strTitle, const CString& strURL, const CString& strTagsText)
{
    CString strText(g_lpszZiwMeta);
    //
    strText.Replace("%title%", strTitle);
    strText.Replace("%url%", strURL);
    strText.Replace("%tags%", strTagsText);
    return strText;
}

QString CWizDocument::GetMetaText()
{
    return WizGetZiwMetaText(m_data.strTitle, m_data.strURL, m_db.GetDocumentTagsText(m_data.strGUID));
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//class CWizFolder
CWizFolder::CWizFolder(CWizDatabase& db, const CString& strLocation)
    : m_db(db)
    , m_strLocation(strLocation)
{

}
bool CWizFolder::IsDeletedItems() const
{
    return m_db.GetDeletedItemsLocation() == Location();
}

bool CWizFolder::IsInDeletedItems() const
{
    return !IsDeletedItems() && Location().startsWith(m_db.GetDeletedItemsLocation());
}

QObject* CWizFolder::CreateDocument2(const QString& strTitle, const QString& strURL)
{
    WIZDOCUMENTDATA data;
    if (!m_db.CreateDocument(strTitle, "", m_strLocation, strURL, data))
        return NULL;

    CWizDocument* pDoc = new CWizDocument(m_db, data);

    return pDoc;
}

void CWizFolder::Delete()
{
    if (IsDeletedItems())
        return;

    if (IsInDeletedItems())
    {
        //if (IDYES != WizMessageBox1(IDS_DELETE_FOLDER, GetName(), MB_YESNO | MB_ICONQUESTION))
        //    return S_FALSE;
        //
        if (!m_db.DeleteDocumentsByLocation(Location()))
        {
            TOLOG1(_T("Failed to delete documents by location; %1"), Location());
            return;
        }
        //
        m_db.LogDeletedFolder(Location());
    }
    else
    {
        CWizFolder deletedItems(m_db, m_db.GetDeletedItemsLocation());
        MoveTo(&deletedItems);
    }
}
void CWizFolder::MoveTo(QObject* dest)
{
    CWizFolder* pFolder = dynamic_cast<CWizFolder*>(dest);
    if (!pFolder)
        return;
    //
    if (IsDeletedItems())
        return;
    //
    if (!CanMove(this, pFolder))
    {
        TOLOG2(_T("Can move %1 to %2"), Location(), pFolder->Location());
        return;
    }
    //
    return MoveToLocation(pFolder->Location());
}

bool CWizFolder::CanMove(const CString& strSrcLocation, const CString& strDestLocation) const
{
    if (m_db.GetDeletedItemsLocation() == strSrcLocation)
        return false;

    if (strDestLocation.startsWith(strSrcLocation))
        return false;

    return true;
}

bool CWizFolder::CanMove(CWizFolder* pSrc, CWizFolder* pDest) const
{
    return CanMove(pSrc->Location(), pDest->Location());
}

void CWizFolder::MoveToLocation(const CString& strDestLocation)
{
    Q_ASSERT(strDestLocation.right(1) == "/");
    Q_ASSERT(strDestLocation.left(1) == "/");

    if (!CanMove(Location(), strDestLocation))
        return;

    CString strOldLocation = Location();
    //CString strNewLocation;
//
    //CString strLocationName = CWizDatabase::GetLocationName(strOldLocation);
//
    //if (strDestLocation.IsEmpty()) {
    //    strNewLocation = "/" + strLocationName + "/";
    //} else {
    //    strNewLocation = strDestLocation + strLocationName + "/";
    //}

    CWizDocumentDataArray arrayDocument;
    if (!m_db.GetDocumentsByLocationIncludeSubFolders(strOldLocation, arrayDocument))
    {
        TOLOG1(_T("Failed to get documents by location (include sub folders): %1"), strOldLocation);
        return;
    }

    CWizDocumentDataArray::const_iterator it;
    for (it = arrayDocument.begin(); it != arrayDocument.end(); it++)
    {
        WIZDOCUMENTDATA data = *it;

        Q_ASSERT(data.strLocation.startsWith(strOldLocation));
        if (!data.strLocation.startsWith(strOldLocation)) {
            TOLOG(_T("Error location of document!"));
            continue;
        }

        data.strLocation.Delete(0, strOldLocation.GetLength());
        data.strLocation.Insert(0, strDestLocation);

        if (!m_db.ModifyDocumentInfo(data))
        {
            TOLOG(_T("Failed to move document to new folder!"));
            continue;
        }
    }

    m_db.LogDeletedFolder(strOldLocation);
}


/* ------------------------------CWizDatabase------------------------------ */

const QString g_strAccountSection = "Account";
const QString g_strCertSection = "Cert";
const QString g_strGroupSection = "Groups";
const QString g_strDatabaseInfoSection = "Database";


CWizDatabase::CWizDatabase()
    : m_ziwReader(new CWizZiwReader())
    , m_nOpenMode(0)
{
}

bool CWizDatabase::setDatabaseInfo(const QString& strKbGUID, const QString& strDatabaseServer,
                                   const QString& strName, int nPermission)
{
    Q_ASSERT(!strKbGUID.isEmpty() && !strName.isEmpty());

    m_strKbGUID = strKbGUID;
    m_strDatabaseServer = strDatabaseServer;

    if (m_strName != strName) {
        m_strName = strName;
        Q_EMIT databaseRename(strKbGUID);
    }

    if (m_nPermission != nPermission) {
        m_nPermission = nPermission;
        Q_EMIT databasePermissionChanged(strKbGUID);
    }

    int nErrors = 0;

    if (!SetMeta(g_strDatabaseInfoSection, "Name", strName))
        nErrors++;

    if (!SetMeta(g_strDatabaseInfoSection, "KbGUID", strKbGUID))
        nErrors++;

    if (!SetMeta(g_strDatabaseInfoSection, "Permission", QString::number(nPermission)))
        nErrors++;

    if (!SetMeta(g_strDatabaseInfoSection, "Version", WIZ_DATABASE_VERSION))
        nErrors++;

    if (nErrors > 0)
        return false;

    return true;
}

bool CWizDatabase::loadDatabaseInfo()
{
    if (m_strKbGUID.isEmpty()) {
        m_strKbGUID = GetMetaDef(g_strDatabaseInfoSection, "KbGUID");
    }

    m_strName = GetMetaDef(g_strDatabaseInfoSection, "Name");
    m_nPermission = GetMetaDef(g_strDatabaseInfoSection, "Permission").toInt();

    if (m_strKbGUID.isEmpty() || m_strName.isEmpty()) {
        return false;
    }

    return true;
}

bool CWizDatabase::openPrivate(const QString& strUserId, const QString& strPassword /* = QString() */)
{
    Q_ASSERT(m_nOpenMode == notOpened);

    m_strUserId = strUserId;
    m_strPassword = strPassword;

    bool ret = CWizIndex::Open(GetUserIndexFileName())
            && CThumbIndex::OpenThumb(GetUserThumbFileName());

    if (ret)
        m_nOpenMode = OpenPrivate;

    loadDatabaseInfo();

    return ret;
}

bool CWizDatabase::openGroup(const QString& strUserId, const QString& strGroupGUID)
{
    Q_ASSERT(m_nOpenMode == notOpened);

    m_strUserId = strUserId;
    m_strKbGUID = strGroupGUID;

    bool ret = CWizIndex::Open(GetGroupIndexFileName())
            && CThumbIndex::OpenThumb(GetGroupThumbFileName());

    if (ret)
        m_nOpenMode = OpenGroup;

    loadDatabaseInfo();

    return ret;
}

QString CWizDatabase::GetAccountPath() const
{
    Q_ASSERT(!m_strUserId.isEmpty());

    QString strPath = ::WizGetDataStorePath()+ m_strUserId + "/";
    WizEnsurePathExists(strPath);

    return strPath;
}

QString CWizDatabase::GetUserDataPath() const
{
   QString strPath = GetAccountPath() + "data/";
   WizEnsurePathExists(strPath);
   return strPath;
}

QString CWizDatabase::GetUserIndexFileName() const
{
    QString strPath = GetUserDataPath();
    return strPath + "index.db";
}

QString CWizDatabase::GetUserThumbFileName() const
{
    QString strPath = GetUserDataPath();
    return strPath + "wizthumb.db";
}

QString CWizDatabase::GetUserDocumentsDataPath() const
{
    QString strPath = GetUserDataPath() + "notes/";
    WizEnsurePathExists(strPath);
    return  strPath;
}

QString CWizDatabase::GetUserAttachmentsDataPath() const
{
    QString strPath = GetUserDataPath() + "attachments/";
    WizEnsurePathExists(strPath);
    return  strPath;
}

QString CWizDatabase::GetGroupDataPath() const
{
    QString strPath = GetAccountPath() + "group/" + m_strKbGUID + "/";
    WizEnsurePathExists(strPath);
    return strPath;
}

QString CWizDatabase::GetGroupIndexFileName() const
{
    QString strPath = GetGroupDataPath();
    return strPath + "index.db";
}

QString CWizDatabase::GetGroupThumbFileName() const
{
    QString strPath = GetGroupDataPath();
    return strPath + "wizthumb.db";
}

QString CWizDatabase::GetGroupDocumentsDataPath() const
{
    CString strPath = GetGroupDataPath() + "notes/";
    WizEnsurePathExists(strPath);
    return  strPath;
}

QString CWizDatabase::GetGroupAttachmentsDataPath() const
{
    QString strPath = GetGroupDataPath() + "attachments/";
    WizEnsurePathExists(strPath);
    return  strPath;
}

QString CWizDatabase::GetDocumentFileName(const CString& strGUID) const
{
    if (m_nOpenMode == OpenPrivate)
        return GetUserDocumentsDataPath() + "{" + strGUID + "}";
    else if (m_nOpenMode == OpenGroup)
        return GetGroupDocumentsDataPath() + "{" + strGUID + "}";
    else
        Q_ASSERT(0);

    return QString();
}

QString CWizDatabase::GetAttachmentFileName(const CString& strGUID) const
{
    if (m_nOpenMode == OpenPrivate)
        return GetUserAttachmentsDataPath() + "{" + strGUID + "}";
    else if (m_nOpenMode == OpenGroup)
        return GetGroupAttachmentsDataPath() + "{" + strGUID + "}";
    else
        Q_ASSERT(0);

    return QString();
}

bool CWizDatabase::GetUserName(QString& strUserName)
{
    strUserName = GetMetaDef(g_strAccountSection, "UserName");
    return true;
}

bool CWizDatabase::SetUserName(const QString& strUserName)
{
    CString strOld;
    GetUserName(strOld);
    if (!strOld.IsEmpty()) {
        TOLOG("Can not set user name: user name exists!");
        return false;
    }

    if (!SetMeta(g_strAccountSection, _T("UserName"), strUserName)) {
        TOLOG("Failed to set user name while SetUserName");
        return false;
    }

    return true;
}

QString CWizDatabase::GetEncryptedPassword()
{
    return GetMetaDef(g_strAccountSection, "Password");
}

bool CWizDatabase::GetPassword(CString& strPassword)
{
    bool bExists = false;
    if (!GetMeta(g_strAccountSection, "Password", strPassword, "", &bExists)) {
        TOLOG("Failed to get password while GetPassword");
        return false;
    }

    if (strPassword.IsEmpty())
        return true;

    strPassword = WizDecryptPassword(strPassword);

    return true;
}

bool CWizDatabase::SetPassword(const QString& strOldPassword, const QString& strPassword)
{
    CString strOld;
    if (!GetPassword(strOld)) {
        TOLOG("Failed to get old password while changing password");
        return false;
    }

    if (strOld != strOldPassword) {
        TOLOG("Invalid password while changing password");
        return false;
    }

    if (!SetMeta(g_strAccountSection, "Password", WizEncryptPassword(strPassword))) {
        TOLOG("Failed to set new password while changing password");
        return false;
    }

    return true;
}

UINT CWizDatabase::GetPasswordFalgs()
{
    CString strFlags = GetMetaDef(g_strAccountSection, "PasswordFlags", "");
    return (UINT)atoi(strFlags.toUtf8());
}

bool CWizDatabase::SetPasswordFalgs(UINT nFlags)
{
    return SetMeta(g_strAccountSection, "PasswordFlags", WizIntToStr(int(nFlags)));
}

bool CWizDatabase::GetUserCert(QString& strN, QString& stre, QString& strd, QString& strHint)
{
    strN = GetMetaDef(g_strCertSection, "N");
    stre = GetMetaDef(g_strCertSection, "E");
    strd = GetMetaDef(g_strCertSection, "D");
    strHint = GetMetaDef(g_strCertSection, "Hint");

    if (strN.isEmpty() || stre.isEmpty() || strd.isEmpty())
        return false;

    return true;
}

bool CWizDatabase::SetUserCert(const QString& strN, const QString& stre, const QString& strd, const QString& strHint)
{
    return SetMeta(g_strCertSection, "N", strN) \
            && SetMeta(g_strCertSection, "E", stre) \
            && SetMeta(g_strCertSection, "D", strd) \
            && SetMeta(g_strCertSection, "Hint", strHint);
}

bool CWizDatabase::setUserInfo(const WIZUSERINFO& userInfo)
{
    int errors = 0;

    if (!SetMeta(g_strAccountSection, "DisplayName", userInfo.strDisplayName))
        errors++;

    if (!SetMeta(g_strAccountSection, "UserType", userInfo.strUserType))
        errors++;

    if (!SetMeta(g_strAccountSection, "UserLevelName", userInfo.strUserLevelName))
        errors++;

    if (!SetMeta(g_strAccountSection, "UserLevel", QString::number(userInfo.nUserLevel)))
        errors++;

    if (!SetMeta(g_strAccountSection, "UserPoints", QString::number(userInfo.nUserPoints)))
        errors++;

    if (errors > 0)
        return false;

    return true;
}

bool CWizDatabase::getUserInfo(WIZUSERINFO& userInfo)
{
    userInfo.strDisplayName = GetMetaDef(g_strAccountSection, "DisplayName");
    userInfo.strUserType = GetMetaDef(g_strAccountSection, "UserType");
    userInfo.strUserLevelName = GetMetaDef(g_strAccountSection, "UserLevelName");
    userInfo.nUserLevel = GetMetaDef(g_strAccountSection, "UserLevel").toInt();
    userInfo.nUserPoints = GetMetaDef(g_strAccountSection, "UserPoints").toInt();

    return true;
}

bool CWizDatabase::setUserGroupInfo(const CWizGroupDataArray& arrayGroup)
{
    if (!deleteMetasByName(g_strGroupSection))
        return false;

    int nTotal = arrayGroup.size();
    if (!nTotal) {
        return false;
    }

    if (!SetMeta(g_strGroupSection, "Count", QString::number(nTotal))) {
        return false;
    }

    int nErrors = 0;
    for (int i = 0; i < nTotal; i++) {
        WIZGROUPDATA group(arrayGroup[i]);
        if (!SetMeta(g_strGroupSection, QString::number(i), group.strGroupGUID)) {
            nErrors++;
        }

        if (!SetMeta(g_strGroupSection, group.strGroupGUID, group.strGroupName)) {
            nErrors++;
        }
    }

    if (nErrors > 0) {
        return false;
    }

    return true;
}

bool CWizDatabase::getUserGroupInfo(CWizGroupDataArray& arrayGroup)
{
    CString strTotal;
    bool bExist;

    if (!GetMeta(g_strGroupSection, "Count", strTotal, "", &bExist)) {
        return false;
    }

    if (!bExist) {
        return false;
    }

    int nTotal = strTotal.toInt();
    for (int i = 0; i < nTotal; i++) {
        QString strGroupGUID = GetMetaDef(g_strGroupSection, QString::number(i));
        QString strGroupName = GetMetaDef(g_strGroupSection, strGroupGUID);

        WIZGROUPDATA group;
        group.strGroupGUID = strGroupGUID;
        group.strGroupName = strGroupName;
        arrayGroup.push_back(group);
    }

    return true;
}


__int64 CWizDatabase::GetObjectVersion(const CString& strObjectName)
{
    return GetMetaInt64("SYNC_INFO", strObjectName, -1) + 1;
}

bool CWizDatabase::SetObjectVersion(const CString& strObjectName, __int64 nVersion)
{
    return SetMetaInt64("SYNC_INFO", strObjectName, nVersion);
}

bool CWizDatabase::UpdateDeletedGUID(const WIZDELETEDGUIDDATA& data)
{
    bool bRet = false;

    CString strType = WIZOBJECTDATA::ObjectTypeToTypeString(data.eType);
    bool bExists = false;
    ObjectExists(data.strGUID, strType, bExists);
    if (!bExists)
        return true;

    bRet = DeleteObject(data.strGUID, strType, false);

    if (!bRet) {
        Q_EMIT updateError("Failed to delete object: " + data.strGUID + " type: " + strType);
    }

    return bRet;
}

bool CWizDatabase::UpdateTag(const WIZTAGDATA& data)
{
    bool bRet = false;

    WIZTAGDATA dataTemp;
    if (TagFromGUID(data.strGUID, dataTemp)) {
        bRet = ModifyTagEx(data);
    } else {
        bRet = CreateTagEx(data);
    }

    if (!bRet) {
        Q_EMIT updateError("Failed to update tag: " + data.strName);
    }

    return bRet;
}

bool CWizDatabase::UpdateStyle(const WIZSTYLEDATA& data)
{
    bool bRet = false;

    WIZSTYLEDATA dataTemp;
    if (StyleFromGUID(data.strGUID, dataTemp)) {
        bRet = ModifyStyleEx(data);
    } else {
        bRet = CreateStyleEx(data);
    }

    if (!bRet) {
        Q_EMIT updateError("Failed to update style: " + data.strName);
    }

    return bRet;
}

bool CWizDatabase::UpdateDocument(const WIZDOCUMENTDATAEX& data)
{
    bool bRet = false;

    WIZDOCUMENTDATAEX dataTemp;
    if (DocumentFromGUID(data.strGUID, dataTemp))
    {
        if (data.nObjectPart & WIZKM_XMLRPC_OBJECT_PART_INFO)
        {
            bRet = ModifyDocumentInfoEx(data);
            if (dataTemp.strDataMD5 != data.strDataMD5)
            {
                SetObjectDataDownloaded(data.strGUID, "document", false);
            }
        }
        else
        {
            bRet = true;
        }
    }
    else
    {
        Q_ASSERT(data.nObjectPart & WIZKM_XMLRPC_OBJECT_PART_INFO);

        bRet = CreateDocumentEx(data);
    }

    if (!bRet) {
        Q_EMIT updateError("Failed to update document: " + data.strTitle);
    }

    WIZDOCUMENTDATA dataRet = data;

    bool resetVersion = false;
    if (!data.arrayParam.empty())
    {
        SetDocumentParams(dataRet, data.arrayParam);
        resetVersion = true;
    }
    if (!data.arrayTagGUID.empty())
    {
        SetDocumentTags(dataRet, data.arrayTagGUID);
        resetVersion = true;
    }

    if (resetVersion)
    {
        //reset document info
        ModifyDocumentInfoEx(data);
    }

    return bRet;
}

bool CWizDatabase::UpdateAttachment(const WIZDOCUMENTATTACHMENTDATAEX& data)
{
    bool bRet = false;

    WIZDOCUMENTATTACHMENTDATAEX dataTemp;
    if (AttachmentFromGUID(data.strGUID, dataTemp))
    {
        bRet = ModifyAttachmentInfoEx(data);

        bool changed = dataTemp.strDataMD5 != data.strDataMD5;
        if (changed)
        {
            SetObjectDataDownloaded(data.strGUID, "attachment", false);
        }
    }
    else
    {
        bRet = CreateAttachmentEx(data);
    }

    if (!bRet) {
        Q_EMIT updateError("Failed to update attachment: " + data.strName);
    }

    return bRet;
}

bool CWizDatabase::UpdateDeletedGUIDs(const std::deque<WIZDELETEDGUIDDATA>& arrayDeletedGUID)
{
    if (arrayDeletedGUID.empty())
        return true;

    __int64 nVersion = -1;

    bool bHasError = false;

    std::deque<WIZDELETEDGUIDDATA>::const_iterator it;
    for (it = arrayDeletedGUID.begin(); it != arrayDeletedGUID.end(); it++)
    {
        const WIZDELETEDGUIDDATA& data = *it;

        if (!UpdateDeletedGUID(data))
        {
            bHasError = true;
        }

        nVersion = std::max<__int64>(nVersion, data.nVersion);
    }

    if (!bHasError)
    {
        SetObjectVersion(WIZDELETEDGUIDDATA::ObjectName(), nVersion);
    }

    return !bHasError;
}

bool CWizDatabase::UpdateTags(const std::deque<WIZTAGDATA>& arrayTag)
{
    if (arrayTag.empty())
        return false;

    __int64 nVersion = -1;

    bool bHasError = false;
    std::deque<WIZTAGDATA>::const_iterator it;
    for (it = arrayTag.begin(); it != arrayTag.end(); it++)
    {
        const WIZTAGDATA& tag = *it;

        Q_EMIT processLog("tag: " + tag.strName);

        if (!UpdateTag(tag))
        {
            bHasError = true;
        }

        nVersion = std::max<__int64>(nVersion, tag.nVersion);
    }
    if (!bHasError)
    {
        SetObjectVersion(WIZTAGDATA::ObjectName(), nVersion);
    }

    return !bHasError;
}


bool CWizDatabase::UpdateStyles(const std::deque<WIZSTYLEDATA>& arrayStyle)
{
    if (arrayStyle.empty())
        return true;

    __int64 nVersion = -1;

    bool bHasError = false;
    std::deque<WIZSTYLEDATA>::const_iterator it;
    for (it = arrayStyle.begin(); it != arrayStyle.end(); it++)
    {
        const WIZSTYLEDATA& data = *it;

        Q_EMIT processLog("style: " + data.strName);

        if (!UpdateStyle(data))
        {
            bHasError = true;
        }

        nVersion = std::max<__int64>(nVersion, data.nVersion);
    }

    if (!bHasError)
    {
        SetObjectVersion(WIZSTYLEDATA::ObjectName(), nVersion);
    }

    return !bHasError;
}

bool CWizDatabase::UpdateDocuments(const std::deque<WIZDOCUMENTDATAEX>& arrayDocument)
{
    if (arrayDocument.empty())
        return true;

    __int64 nVersion = -1;

    bool bHasError = false;
    std::deque<WIZDOCUMENTDATAEX>::const_iterator it;
    for (it = arrayDocument.begin(); it != arrayDocument.end(); it++)
    {
        const WIZDOCUMENTDATAEX& data = *it;

        Q_EMIT processLog("note: " + data.strTitle);

        if (!UpdateDocument(data))
        {
            bHasError = true;
        }

        nVersion = std::max<__int64>(nVersion, data.nVersion);
    }

    if (!bHasError)
    {
        SetObjectVersion(WIZDOCUMENTDATAEX::ObjectName(), nVersion);
    }

    return !bHasError;
}

bool CWizDatabase::UpdateAttachments(const std::deque<WIZDOCUMENTATTACHMENTDATAEX>& arrayAttachment)
{
    if (arrayAttachment.empty())
        return true;

    __int64 nVersion = -1;

    bool bHasError = false;

    std::deque<WIZDOCUMENTATTACHMENTDATAEX>::const_iterator it;
    for (it = arrayAttachment.begin(); it != arrayAttachment.end(); it++)
    {
        const WIZDOCUMENTATTACHMENTDATAEX& data = *it;

        Q_EMIT processLog("attachment: " + data.strName);

        if (!UpdateAttachment(data))
        {
            bHasError = true;
        }

        nVersion = std::max<__int64>(nVersion, data.nVersion);
    }

    if (!bHasError)
    {
        SetObjectVersion(WIZDOCUMENTATTACHMENTDATAEX::ObjectName(), nVersion);
    }

    return !bHasError;
}

bool CWizDatabase::UpdateDocumentData(WIZDOCUMENTDATA& data, const QString& strHtml, const QString& strURL, int nFlags)
{
    CString strProcessedHtml(strHtml);
    CString strResourcePath = GetResoucePathFromFile(strURL);
    if (!strResourcePath.IsEmpty()) {
        QUrl urlResource = QUrl::fromLocalFile(strResourcePath);
        strProcessedHtml.replace(urlResource.toString(), "index_files/");
    }

    CWizDocument doc(*this, data);
    CString strMetaText = doc.GetMetaText();

    CString strZipFileName = GetDocumentFileName(data.strGUID);
    if (!data.nProtected) {
        bool bZip = ::WizHtml2Zip(strURL, strProcessedHtml, strResourcePath, nFlags, strMetaText, strZipFileName);
        if (!bZip) {
            return false;
        }
    } else {
        CString strTempFile = ::WizGlobal()->GetTempPath() + data.strGUID + "-decrypted";
        bool bZip = ::WizHtml2Zip(strURL, strProcessedHtml, strResourcePath, nFlags, strMetaText, strTempFile);
        if (!bZip) {
            return false;
        }

        if (!m_ziwReader->encryptDataToTempFile(strTempFile, strZipFileName)) {
            return false;
        }
    }

    SetObjectDataDownloaded(data.strGUID, "document", true);

    return UpdateDocumentDataMD5(data, strZipFileName);
}

bool CWizDatabase::UpdateDocumentDataMD5(WIZDOCUMENTDATA& data, const CString& strZipFileName)
{
    bool bRet = CWizIndex::UpdateDocumentDataMD5(data, strZipFileName);

    UpdateDocumentAbstract(data.strGUID);

    return bRet;
}
bool CWizDatabase::DeleteAttachment(const WIZDOCUMENTATTACHMENTDATA& data, bool bLog)
{
    bool bRet = CWizIndex::DeleteAttachment(data, bLog);
    CString strFileName = GetAttachmentFileName(data.strGUID);
    if (PathFileExists(strFileName))
    {
        ::WizDeleteFile(strFileName);
    }
    return bRet;
}

bool CWizDatabase::IsDocumentDownloaded(const CString& strGUID)
{
    return IsObjectDataDownloaded(strGUID, "document");
}

bool CWizDatabase::IsAttachmentDownloaded(const CString& strGUID)
{
    return IsObjectDataDownloaded(strGUID, "attachment");
}

bool CWizDatabase::GetAllObjectsNeedToBeDownloaded(std::deque<WIZOBJECTDATA>& arrayData)
{
    CWizDocumentDataArray arrayDocument;
    CWizDocumentAttachmentDataArray arrayAttachment;
    GetNeedToBeDownloadedDocuments(arrayDocument);
    GetNeedToBeDownloadedAttachments(arrayAttachment);

    arrayData.assign(arrayAttachment.begin(), arrayAttachment.end());
    arrayData.insert(arrayData.begin(), arrayDocument.begin(), arrayDocument.end());

    return true;
}
bool CWizDatabase::UpdateSyncObjectLocalData(const WIZOBJECTDATA& data)
{
    if (data.eObjectType == wizobjectDocumentAttachment)
    {
        if (!SaveCompressedAttachmentData(data.strObjectGUID, data.arrayData))
        {
            Q_EMIT updateError("Failed to save attachment data: " + data.strDisplayName);
            return false;
        }
    }
    else
    {
        CString strFileName = GetObjectFileName(data);
        if (!::WizSaveDataToFile(strFileName, data.arrayData))
        {
            Q_EMIT updateError("Failed to save document data: " + data.strDisplayName);
            return false;
        }
    }

    if (data.eObjectType == wizobjectDocument)
    {
        WIZDOCUMENTDATA document;
        if (DocumentFromGUID(data.strObjectGUID, document))
        {
            Q_EMIT documentDataModified(document);
        }

        UpdateDocumentAbstract(data.strObjectGUID);
    }

    SetObjectDataDownloaded(data.strObjectGUID, WIZOBJECTDATA::ObjectTypeToTypeString(data.eObjectType), true);

    return true;
}


CString CWizDatabase::GetObjectFileName(const WIZOBJECTDATA& data)
{
    if (data.eObjectType == wizobjectDocument)
        return GetDocumentFileName(data.strObjectGUID);
    else if (data.eObjectType == wizobjectDocumentAttachment)
        return GetAttachmentFileName(data.strObjectGUID);
    else
    {
        ATLASSERT(false);
        return CString();
    }
}

bool CWizDatabase::GetAllRootLocations(const CWizStdStringArray& arrayAllLocation, \
                                       CWizStdStringArray& arrayLocation)
{
    std::set<QString> setRoot;

    CWizStdStringArray::const_iterator it;
    for (it = arrayAllLocation.begin(); it != arrayAllLocation.end(); it++) {
        setRoot.insert(GetRootLocation(*it));
    }

    arrayLocation.assign(setRoot.begin(), setRoot.end());

    return true;
}

bool CWizDatabase::GetChildLocations(const CWizStdStringArray& arrayAllLocation, \
                                     const QString& strLocation, \
                                     CWizStdStringArray& arrayLocation)
{
    if (strLocation.isEmpty())
        return GetAllRootLocations(arrayAllLocation, arrayLocation);

    std::set<QString> setLocation;

    CWizStdStringArray::const_iterator it;
    for (it = arrayAllLocation.begin(); it != arrayAllLocation.end(); it++) {
        const QString& str = *it;

        if (str.length() > strLocation.length() && str.startsWith(strLocation))
        {
            int index = str.indexOf('/', strLocation.length() + 1);
            if (index > 0)
            {
                QString strChild = str.left(index + 1);
                setLocation.insert(strChild);
            }
        }
    }

    arrayLocation.assign(setLocation.begin(), setLocation.end());

    return true;
}

bool CWizDatabase::IsInDeletedItems(const CString& strLocation)
{
    return strLocation.startsWith(GetDeletedItemsLocation());
}

bool CWizDatabase::CreateDocumentAndInit(const CString& strHtml, \
                                         const CString& strHtmlUrl, \
                                         int nFlags, \
                                         const CString& strTitle, \
                                         const CString& strName, \
                                         const CString& strLocation, \
                                         const CString& strURL, \
                                         WIZDOCUMENTDATA& data)
{
    bool bRet = false;
    try
    {
        BeginUpdate();

        data.strKbGUID = kbGUID();
        bRet = CreateDocument(strTitle, strName, strLocation, strHtmlUrl, data);
        if (bRet)
        {
            bRet = UpdateDocumentData(data, strHtml, strURL, nFlags);

            Q_EMIT documentCreated(data);
        }
    }
    catch (...)
    {

    }

    EndUpdate();

    return bRet;
}

bool CWizDatabase::AddAttachment(const WIZDOCUMENTDATA& document, const CString& strFileName, WIZDOCUMENTATTACHMENTDATA& dataRet)
{
    CString strMD5 = ::WizMd5FileString(strFileName);
    if (!CreateAttachment(document.strGUID, WizExtractFileName(strFileName), strFileName, "", strMD5, dataRet))
        return false;

    if (!::WizCopyFile(strFileName, GetAttachmentFileName(dataRet.strGUID), false))
    {
        DeleteAttachment(dataRet, false);
        return false;
    }

    SetAttachmentDataDownloaded(dataRet.strGUID, true);
    UpdateDocumentAttachmentCount(document.strGUID);

    return true;
}

bool CWizDatabase::DeleteTagWithChildren(const WIZTAGDATA& data, bool bLog)
{
    CWizTagDataArray arrayChildTag;
    GetChildTags(data.strGUID, arrayChildTag);
    foreach (const WIZTAGDATA& childTag, arrayChildTag)
    {
        DeleteTagWithChildren(childTag, bLog);
    }

    DeleteTag(data, bLog);

    return true;
}

bool CWizDatabase::LoadDocumentData(const CString& strDocumentGUID, QByteArray& arrayData)
{
    CString strFileName = GetDocumentFileName(strDocumentGUID);
    if (!PathFileExists(strFileName))
    {
        return false;
    }

    QFile file(strFileName);
    if (!file.open(QFile::ReadOnly))
        return false;

    arrayData = file.readAll();

    return !arrayData.isEmpty();
}

bool CWizDatabase::LoadAttachmentData(const CString& strDocumentGUID, QByteArray& arrayData)
{
    CString strFileName = GetAttachmentFileName(strDocumentGUID);
    if (!PathFileExists(strFileName))
    {
        return false;
    }

    QFile file(strFileName);
    if (!file.open(QFile::ReadOnly))
        return false;

    arrayData = file.readAll();

    return !arrayData.isEmpty();
}

bool CWizDatabase::LoadCompressedAttachmentData(const CString& strDocumentGUID, QByteArray& arrayData)
{
    CString strFileName = GetAttachmentFileName(strDocumentGUID);
    if (!PathFileExists(strFileName))
    {
        return false;
    }
    CString strTempZipFileName = ::WizGlobal()->GetTempPath() + WizIntToStr(GetTickCount()) + ".tmp";
    CWizZipFile zip;
    if (!zip.open(strTempZipFileName))
    {
        Q_EMIT updateError("Failed to create temp zip file: " + strTempZipFileName);
        return false;
    }

    if (!zip.compressFile(strFileName, "data"))
    {
        Q_EMIT updateError("Failed to compress file: " + strFileName);
        return false;
    }

    zip.close();

    QFile file(strTempZipFileName);
    if (!file.open(QFile::ReadOnly))
        return false;

    arrayData = file.readAll();

    return !arrayData.isEmpty();
}

bool CWizDatabase::SaveCompressedAttachmentData(const CString& strDocumentGUID, const QByteArray& arrayData)
{
    CString strTempZipFileName = ::WizGlobal()->GetTempPath() + WizIntToStr(GetTickCount()) + ".tmp";
    if (!WizSaveDataToFile(strTempZipFileName, arrayData))
    {
        Q_EMIT updateError("Failed to save attachment data to temp file: " + strTempZipFileName);
        return false;
    }
    CWizUnzipFile zip;
    if (!zip.open(strTempZipFileName))
    {
        Q_EMIT updateError("Failed to open temp zip file: " + strTempZipFileName);
        return false;
    }

    CString strFileName = GetAttachmentFileName(strDocumentGUID);
    if (!zip.extractFile(0, strFileName))
    {
        Q_EMIT updateError("Failed to extract attachment file: " + strFileName);
        return false;
    }

    zip.close();

    return true;
}

bool CWizDatabase::UpdateDocumentAbstract(const CString& strDocumentGUID)
{
    CString strFileName = GetDocumentFileName(strDocumentGUID);
    if (!PathFileExists(strFileName)) {
        return false;
    }

    WIZDOCUMENTDATA data;
    if (!DocumentFromGUID(strDocumentGUID, data)) {
        return false;
    }

    if (data.nProtected) {
        return false;
    }

    CString strHtmlFileName;
    if (!DocumentToTempHtmlFile(data, strHtmlFileName))
        return false;

    CString strHtmlTempPath = WizExtractFilePath(strHtmlFileName);

    CString strHtml;
    CString strAbstractFileName = strHtmlTempPath + "wiz_full.html";
    if (PathFileExists(strAbstractFileName)) {
        ::WizLoadUnicodeTextFromFile(strAbstractFileName, strHtml);
    } else {
        ::WizLoadUnicodeTextFromFile(strHtmlFileName, strHtml);
    }

    WIZABSTRACT abstract;
    abstract.guid = strDocumentGUID;
    ::WizHtml2Text(strHtml, abstract.text);

    abstract.text.replace("\r", "");
    abstract.text.replace("\t", " ");
    abstract.text.replace(QRegExp("[\\n]+"), " ");
    abstract.text.replace(QRegExp("[\\s]+"), " ");
    if (abstract.text.length() > 2000)
    {
        abstract.text = abstract.text.left(2000);
    }

    CString strResourcePath = WizExtractFilePath(strHtmlFileName) + "index_files/";
    CWizStdStringArray arrayImageFileName;
    ::WizEnumFiles(strResourcePath, "*.jpg;*.png;*.bmp;*.gif", arrayImageFileName, 0);
    if (!arrayImageFileName.empty())
    {
        CString strImageFileName = arrayImageFileName[0];
        //DEBUG_TOLOG1(_T("abstract image file: %1"), strImageFileName);

        qint64 m = 0;
        for (CWizStdStringArray::const_iterator it = arrayImageFileName.begin() + 1;
        it != arrayImageFileName.end();
        it++)
        {
            CString strFileName = *it;
            qint64 size = ::WizGetFileSize(strFileName);
            if (size > m)
            {
                strImageFileName = strFileName;
                m = size;
            }
        }

        QImage img;
        if (img.load(strImageFileName))
        {
            //DEBUG_TOLOG2("Abstract image size: %1 X %2", WizIntToStr(img.width()), WizIntToStr(img.height()));
            if (img.width() > 32 && img.height() > 32)
            {
                abstract.image = img;
            }
        }
        else
        {
            Q_EMIT updateError("Failed to load image file: " + strImageFileName);
        }
    }


    bool ret = UpdatePadAbstract(abstract);
    if (!ret)
    {
        Q_EMIT updateError("Failed to update note abstract!");
    }

    ::WizDeleteFolder(strHtmlTempPath);

    Q_EMIT documentAbstractModified(data);

    return ret;
}

CString CWizDatabase::GetRootLocation(const CString& strLocation)
{
    int index = strLocation.indexOf('/', 1);
    if (index == -1)
        return CString();

    return strLocation.left(index + 1);
}

CString CWizDatabase::GetLocationName(const CString& strLocation)
{
    int index = strLocation.lastIndexOf('/', strLocation.length() - 2);
    if (index == -1)
        return CString();

    CString str = strLocation.right(strLocation.length() - index - 1);

    str.Trim('/');

    return str;
}

CString CWizDatabase::GetLocationDisplayName(const CString& strLocation)
{
    if (IsRootLocation(strLocation)) {
        if (strLocation.startsWith("/My ")) {
            if (strLocation == "/My Notes/") {
                return tr("My Notes");
            } else if (strLocation == "/My Journals/") {
                return tr("My Journals");
            } else if (strLocation == "/My Events/") {
                return tr("My Events");
            } else if (strLocation == "/My Sticky Notes/") {
                return tr("My Sticky Notes");
            } else if (strLocation == "/My Emails/") {
                return tr("My Emails");
            } else if (strLocation == "/My Drafts/") {
                return tr("My Drafts");
            } else if (strLocation == "/My Tasks/") {
                return tr("My Tasks");
            }
        }
    } else if (strLocation == "/My Tasks/Inbox/") {
        return tr("Inbox");
    } else if (strLocation == "/My Tasks/Completed/") {
        return tr("Completed");
    }

    return GetLocationName(strLocation);
}

bool CWizDatabase::GetDocumentsByTag(const WIZTAGDATA& tag, CWizDocumentDataArray& arrayDocument)
{
    return CWizIndex::GetDocumentsByTag("", tag, arrayDocument, false);
}

bool CWizDatabase::loadUserCert()
{
    QString strN, stre, strEncryptedd, strHint;
    if (!GetUserCert(strN, stre, strEncryptedd, strHint)) {
        TOLOG("can't get user cert from db");
        return false;
    }

    m_ziwReader->setRSAKeys(strN.toUtf8(), stre.toUtf8(), strEncryptedd.toUtf8(), strHint);
    return true;
}

bool CWizDatabase::DocumentToTempHtmlFile(const WIZDOCUMENTDATA& document, QString& strTempHtmlFileName)
{
    CString strZipFileName = GetDocumentFileName(document.strGUID);
    if (!PathFileExists(strZipFileName)) {
        return false;
    }

    CString strTempPath = ::WizGlobal()->GetTempPath() + document.strGUID + "/";
    ::WizEnsurePathExists(strTempPath);

    if (document.nProtected) {
        if (userCipher().isEmpty()) {
            return false;
        }

        if (!m_ziwReader->setFile(strZipFileName)) {
            return false;
        }

        strZipFileName = ::WizGlobal()->GetTempPath() + document.strGUID + "-decrypted";
        QFile::remove(strZipFileName);

        if (!m_ziwReader->decryptDataToTempFile(strZipFileName)) {
            // force clear usercipher
            m_ziwReader->setUserCipher(QString());
            return false;
        }
    }

    CWizUnzipFile::extractZip(strZipFileName, strTempPath);

    strTempHtmlFileName = strTempPath + "index.html";

    QString strText;
    ::WizLoadUnicodeTextFromFile(strTempHtmlFileName, strText);

    QUrl url = QUrl::fromLocalFile(strTempPath + "index_files/");
    strText.replace("index_files/", url.toString());

    WizSaveUnicodeTextToUtf8File(strTempHtmlFileName, strText);

    return PathFileExists(strTempHtmlFileName);
}

QObject* CWizDatabase::GetFolderByLocation(const QString& strLocation, bool create)
{
    Q_UNUSED(create);

    return new CWizFolder(*this, strLocation);
}

QObject* CWizDatabase::GetDeletedItemsFolder()
{
    return new CWizFolder(*this, GetDeletedItemsLocation());
}

QObject* CWizDatabase::DocumentFromGUID(const QString& strGUID)
{
    WIZDOCUMENTDATA data;
    if (!DocumentFromGUID(strGUID, data))
        return NULL;

    CWizDocument* pDoc = new CWizDocument(*this, data);
    return pDoc;
}

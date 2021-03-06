/*
 Copyright (c) 2017-2018 (c) Project "DeM Labs Inc" https://github.com/demlabsinc
  All rights reserved.

 This file is part of DAP (Deus Applications Prototypes) the open source project

    DAP (Deus Applicaions Prototypes) is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/


#define OP_CODE_LOGIN_INCORRECT_PSWD  "0xf2"
#define OP_CODE_NOT_FOUND_LOGIN_IN_DB "0xf3"
#define OP_CODE_SUBSCRIBE_EXPIRIED    "0xf4"
#define OP_CODE_CANT_CONNECTION_TO_DB "0xf5"
#define OP_CODE_INCORRECT_SYM         "0xf6"

#include "DapSession.h"
#include "DapCrypt.h"
#include "DapReplyTimeout.h"
#include "msrln/msrln.h"
#include <QJsonDocument>
#include <QJsonObject>

const QString DapSession::URL_ENCRYPT("/1901248124123459");
const QString DapSession::URL_STREAM("/874751843144");
const QString DapSession::URL_DB("/01094787531354");
const QString DapSession::URL_CTL("/091348758013553");
const QString DapSession::URL_DB_FILE("/98971341937495431398");
const QString DapSession::URL_SERVER_LIST("/slist");

#define SESSION_KEY_ID_LEN 33

DapSession::DapSession(QObject * obj, int requestTimeout) :
    QObject(obj), m_requestTimeout(requestTimeout)
{
    m_dapCrypt = new DapCrypt;
}

DapSession::~DapSession()
{
    delete m_dapCrypt;
}

QNetworkReply * DapSession::streamOpenRequest(const QString& subUrl, const QString& query)
{
    if(m_sessionKeyID.isEmpty()) {
        qCritical() << "Can't send request to server."
                       "Session was not be initialized";
        return Q_NULLPTR;
    }

    QByteArray subUrlEncrypted, queryEncrypted;

    QByteArray subUrlByte = subUrl.toLatin1();
    QByteArray queryByte = query.toLatin1();

    m_dapCrypt->encode(subUrlByte, subUrlEncrypted, KeyRoleSession);
    m_dapCrypt->encode(queryByte, queryEncrypted, KeyRoleSession);

    QString str_url = QString("%1/%2?%3").arg(URL_CTL)
            .arg(QString(subUrlEncrypted.toBase64(QByteArray::Base64UrlEncoding)))
            .arg(QString(queryEncrypted.toBase64(QByteArray::Base64UrlEncoding)));

    return _buildNetworkReplyReq(str_url, Q_NULLPTR);
}

QNetworkReply* DapSession::_buildNetworkReplyReq(const QString& urlPath,
                                                 const QByteArray* data)
{
    QVector<HttpRequestHeader> headers;
    fillSessionHttpHeaders(headers);
    QNetworkReply* result;
    if(data) {
        result =  DapConnectClient::instance()->request_POST(m_upstreamAddress,
                                                             m_upstreamPort,
                                                             urlPath, *data,
                                                             false, &headers);
    } else {
        result =  DapConnectClient::instance()->request_GET(m_upstreamAddress,
                                                            m_upstreamPort,
                                                            urlPath, false, &headers);
    }

    DapReplyTimeout::set(result, m_requestTimeout);
    return result;
}

/**
 * @brief DapSession::requestServerPublicKey
 */
QNetworkReply* DapSession::requestServerPublicKey()
{
    QByteArray reqData = m_dapCrypt->generateAliceMessage().toBase64();

    m_netEncryptReply = _buildNetworkReplyReq(URL_ENCRYPT + "/gd4y5yh78w42aaagh",
                                              &reqData);

    if(!m_netEncryptReply){
        qWarning() << "Can't send post request";
        return Q_NULLPTR;
    }

    connect(m_netEncryptReply, &QNetworkReply::finished, this, &DapSession::onEnc);

    emit pubKeyRequested();

    return m_netEncryptReply;
}

QNetworkReply* DapSession::encryptInitRequest()
{
    return requestServerPublicKey();
}


/**
 * @brief DapSession::onEnc
 */
void DapSession::onEnc()
{
    qDebug() << "On Enc()";

    QByteArray arrData;
    arrData.append(m_netEncryptReply->readAll());
    if(arrData.isEmpty()) {
        qWarning() << "Empty buffer in onEnc";
        if(m_netEncryptReply->error() == QNetworkReply::NoError) {
            qCritical() << "No error and empty buffer!";
        } else {
            errorSlt(m_netEncryptReply->error());
        }
        return;
    }

    QJsonParseError json_err;
    auto json_resp = QJsonDocument::fromJson(arrData, &json_err);
    if(json_err.error != QJsonParseError::NoError) {
        QString errorMessage = "Can't parse response from server";
        qCritical() << errorMessage << json_err.errorString();
        emit errorEncryptInitialization(errorMessage);
        return;
    }

    auto json_err_resp = json_resp.object().value("error");
    if(json_err_resp != QJsonValue::Undefined) {
        QString serverErrorMsg = json_err_resp.toString();
        qCritical() << "Got error message from server:"
                    << json_err_resp.toString();
        emit errorEncryptInitialization(serverErrorMsg);
        return;
    }

    auto json_encrypt_id = json_resp.object().value("encrypt_id");
    auto json_encrypt_msg = json_resp.object().value("encrypt_msg");
    if(json_encrypt_id == QJsonValue::Undefined ||
            json_encrypt_msg == QJsonValue::Undefined) {
        QString errorMessage = "Bad response from server";
        emit errorEncryptInitialization(errorMessage);
        return;
    }

    m_sessionKeyID = QByteArray::fromBase64(json_encrypt_id.toString().toLatin1());
    QByteArray bobMsg = QByteArray::fromBase64(json_encrypt_msg.toString().toLatin1());

    if (bobMsg.size() != MSRLN_PKB_BYTES) {
        QString errorMessage = "Bad length encrypt message from server";
        qCritical() << "Server Bob message is failed, length = " << bobMsg.length();
        emit errorEncryptInitialization(errorMessage);
        return;
    }

    if(!m_dapCrypt->generateSharedSessionKey(bobMsg, m_sessionKeyID.toLatin1())) {
        QString errorMessage = "Failed generate session key";
        emit errorEncryptInitialization("Failed generate session key");
        return;
    }

    emit encryptInitialized();
}

void DapSession::fillSessionHttpHeaders(HttpHeaders& headers) const
{
    auto setHeader = [&](const QString& field, const QString& value) {
        if(!value.isEmpty()) {
            headers.append({field, value});
        }
    };

    setHeader("Content-Type","text/plain");
    setHeader("Cookie", m_cookie);
    setHeader("KeyID", m_sessionKeyID);
    setHeader("User-Agent", m_userAgent);
}

void DapSession::setUserAgent(const QString& userAgent)
{
    m_userAgent = userAgent;
}

/**
 * @brief DapSession::encRequest
 * @param dcb
 * @param reqData
 * @param url
 * @param subUrl
 * @param query
 * @return
 */
QNetworkReply* DapSession::encRequest(const QString& reqData, const QString& url,
                          const QString& subUrl, const QString& query)
{
    QByteArray BAreqData = reqData.toLatin1();
    QByteArray BAreqDataEnc;
    QByteArray BAsubUrlEncrypted;
    QByteArray BAqueryEncrypted;
    QByteArray subUrlByte = subUrl.toLatin1();
    QByteArray queryByte = query.toLatin1();

    m_dapCrypt->encode(BAreqData, BAreqDataEnc, KeyRoleSession);

    QString urlPath = url;
    if(subUrl.length()) {
        m_dapCrypt->encode(subUrlByte, BAsubUrlEncrypted, KeyRoleSession);
        urlPath += "/" + BAsubUrlEncrypted.toBase64(QByteArray::Base64UrlEncoding);
    }
    if(query.length()) {
        m_dapCrypt->encode(queryByte, BAqueryEncrypted, KeyRoleSession);
        urlPath += "?" + BAqueryEncrypted.toBase64(QByteArray::Base64UrlEncoding);
    }

    return _buildNetworkReplyReq(urlPath, &BAreqDataEnc);
}

/**
 * @brief DapSession::setSaUri
 * @param saUri
 */
void DapSession::setDapUri(const QString& addr, const uint16_t port)
{
    qDebug() << "DapSession set Uri" << addr << port;
    m_upstreamAddress = addr;
    m_upstreamPort = port;
}

/**
 * @brief DapSession::onAuthorize
 */
void DapSession::onAuthorize()
{
    QByteArray arrData;
    arrData.append(m_netAuthorizeReply->readAll());

    if(arrData.size() <= 0)
    {
        emit errorAuthorization("Wrong answer from server");
        return;
    }

    QByteArray arrData2 = QByteArray::fromBase64(arrData);

    QByteArray dByteArr;
    m_dapCrypt->decode(arrData, dByteArr, KeyRoleSession);

    QXmlStreamReader m_xmlStreamReader;
    m_xmlStreamReader.addData(dByteArr);
    qDebug() << "[DapSession] Decoded data: " << QString::fromLatin1(dByteArr);

    if (QString::fromLatin1(dByteArr) == OP_CODE_NOT_FOUND_LOGIN_IN_DB) {
        emit errorAuthorization ("Login not found in database");
        return;
    } else if (QString::fromLatin1(dByteArr) == OP_CODE_LOGIN_INCORRECT_PSWD) {
        emit errorAuthorization ("Incorrect password");
        return;
    } else if (QString::fromLatin1(dByteArr) == OP_CODE_SUBSCRIBE_EXPIRIED) {
        emit errorAuthorization ("Subscribe expired");
        return;
    } else if (QString::fromLatin1(dByteArr) == OP_CODE_CANT_CONNECTION_TO_DB) {
        emit errorAuthorization ("Can't connect to database");
        return;
    } else if (QString::fromLatin1(dByteArr) == OP_CODE_INCORRECT_SYM){
        emit errorAuthorization("Incorrect symbols in request");
        return;
    }

    bool isCookie = false;
    QString SRname;
    while(m_xmlStreamReader.readNextStartElement())
    {
        qDebug() << " name = " << m_xmlStreamReader.name();
        if(SRname == "err_str") {
            QString error_text = m_xmlStreamReader.readElementText();
            qDebug() << " Error str = " << error_text;
            emit errorAuthorization(QString("Server replied error string: '%1'").arg(error_text));
            return;
        }

        if (m_xmlStreamReader.name() == "auth_info") {
            while(m_xmlStreamReader.readNextStartElement()) {
                qDebug() << " auth_info = " << m_xmlStreamReader.name();
                if (m_xmlStreamReader.name() == "cookie") {
                    m_cookie = m_xmlStreamReader.readElementText();
                    qDebug() << "m_cookie: " << m_cookie;
                    isCookie = true;
                    //requestServerList();
                    emit authorized(m_cookie);
                } else {
                    m_userInform[m_xmlStreamReader.name().toString()] = m_xmlStreamReader.readElementText();
                    qDebug() << "Add user information: " << m_xmlStreamReader.name().toString()
                             << m_userInform[m_xmlStreamReader.name().toString()];
                }
            }
        } else {
            m_xmlStreamReader.skipCurrentElement();
        }
    }

    if(!isCookie) {
        m_cookie.clear();
        emit errorAuthorization("No authorization cookie in server's reply");
    }
}

/**
 * @brief DapSession::onLogout
 */
void DapSession::onLogout()
{
    qInfo() << "Logouted on the remote server";
    emit logouted();
}

void DapSession::clearCredentials()
{
    qDebug() << "clearCredentials()";
    m_cookie.clear();
    m_sessionKeyID.clear();
}

/**
 * @brief DapSession::logout
 */
QNetworkReply * DapSession::logoutRequest()
{
    qDebug() << "Request for logout";
    QNetworkReply * netReply = encRequest("", URL_DB, "auth", "logout", SLOT(onLogout()));
    clearCredentials();
    m_upstreamAddress.clear();
    m_upstreamPort = 0;
    emit logoutRequested();
    return netReply;
}

/**
 * @brief DapSession::encRequest
 * @param reqData
 * @param url
 * @param subUrl
 * @param query
 * @param obj
 * @param slot
 */
QNetworkReply * DapSession::encRequest(const QString& reqData, const QString& url, const QString& subUrl,
                           const QString& query, QObject * obj, const char * slot)
{
    QNetworkReply * netReply = encRequest(reqData, url, subUrl, query);

    connect(netReply, SIGNAL(finished()), obj, slot);
    connect(netReply, SIGNAL(error(QNetworkReply::NetworkError)),
            this,SLOT(errorSlt(QNetworkReply::NetworkError)));

    return netReply;
}

/**
 * @brief DapSession::authorize
 * @param user
 * @param password
 * @param domain
 */
QNetworkReply * DapSession::authorizeRequest(const QString& user, const QString& password,const QString& domain)
{
    m_user = user;
    m_userInform.clear();
    m_netAuthorizeReply = encRequest(user + " " + password + " " + domain,
                                     URL_DB, "auth", "login", SLOT(onAuthorize()));
    if(m_netAuthorizeReply == Q_NULLPTR) {
        qCritical() << "Can't send authorize request";
        return Q_NULLPTR;
    }
    emit authRequested();
    return m_netAuthorizeReply;
}

/**
 * @brief DapSession::errorSlt
 * @param error
 */
void DapSession::errorSlt(QNetworkReply::NetworkError error)
{
    qWarning() << "Error: " << error;
    switch(error) {
        case QNetworkReply::NoError: // Do nothing. No error
        break;
        case QNetworkReply::ConnectionRefusedError:
            emit errorNetwork("connection refused");
            break;
        case QNetworkReply::HostNotFoundError:
            emit errorNetwork("Network error: host not found");
            break;
        case QNetworkReply::TimeoutError:
            emit errorNetwork("Network error: timeout, may be host is down?");
            break;
        case QNetworkReply::TemporaryNetworkFailureError:
            emit errorNetwork("Network error: Temprorary network problems, reply request as soon as the network connection is re-established");
            break;
        case QNetworkReply::NetworkSessionFailedError:
            emit errorNetwork("Network error: No network connection");
            break;
        case QNetworkReply::BackgroundRequestNotAllowedError:
            emit errorNetwork("Network error: background request are not permitted by OS");
            break;
        case QNetworkReply::ProxyConnectionRefusedError:
            emit errorNetwork("Network error: Proxy refused to connection");
            break;
        case QNetworkReply::ProxyNotFoundError:
            emit errorNetwork(tr("Proxy server is not found"));
            break;
        case QNetworkReply::ProxyTimeoutError:
            emit errorNetwork(tr("Proxy server timout, is it up?"));
            break;
        case QNetworkReply::ProxyAuthenticationRequiredError:
            emit errorNetwork(tr("Authorization problem"));
            break;
        default:{
            emit errorNetwork(tr("Undefined network error"));
        }
    }
}

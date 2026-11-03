//////////////////////////////////////////////////////////////////////
//
// BeeBEEP Copyright (C) 2010-2026 Marco Mastroddi
//
// BeeBEEP is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version.
//
// BeeBEEP is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with BeeBEEP. If not, see <http://www.gnu.org/licenses/>.
//
// Author: Marco Mastroddi <marco.mastroddi(AT)gmail.com>
//
// $Id: GAnalytics.cpp 1592 2026-08-11 10:14:22Z mastroddi $
//
//////////////////////////////////////////////////////////////////////

#include "GAnalytics.h"
#include "Settings.h"
#include <QRegularExpression>


GAnalytics::GAnalytics( QObject* parent )
 : QObject( parent )
{
  setObjectName( "BeeBEEP Analytics" );
  mp_manager = new QNetworkAccessManager( this );
  connect( mp_manager, SIGNAL( finished( QNetworkReply* ) ), this, SLOT( onReplyFinished( QNetworkReply* ) ) );
}

void GAnalytics::doPost()
{
#ifdef BEEBEEP_DEBUG
  qDebug() << "Sending statistics to" << qPrintable( Settings::instance().gaUrl() );
#endif

  QUrl ga_url = QUrl::fromUserInput( Settings::instance().gaUrl() );

  // Proxy configuration
  QNetworkProxyQuery npq = QNetworkProxyQuery( ga_url );
  QNetworkProxy np = Settings::instance().systemNetworkProxy( npq );
  if( np.type() != QNetworkProxy::NoProxy )
    mp_manager->setProxy( np );

  // Request headers setup (GA4 requires application/json)
  QNetworkRequest req( ga_url );
  req.setHeader( QNetworkRequest::ContentTypeHeader, "application/json" );

  // Construct event parameters
  QJsonObject eventParams;
  eventParams.insert( "app_version", Settings::instance().version( false, false, false ) );

  QString user_language = QLocale::system().name().toLower();
  if( !user_language.isEmpty() )
    eventParams.insert( "language", user_language );

#ifdef BEEBEEP_DEBUG
  eventParams.insert( "os_version", QString( "zz_test-%1" ).arg( Settings::instance().version( false, false, false ) ) );
  // Enable DebugView in GA4 for debug builds
  eventParams.insert( "debug_mode", true );
#else
  eventParams.insert( "os_version", Settings::instance().operatingSystem( false ).toLower() );
#endif
  eventParams.insert( "engagement_time_msec", 100 );

  qint64 days_used = qMax( static_cast<qint64>(0), static_cast<qint64>(Settings::instance().settingsCreationDate().daysTo( QDate::currentDate() ) ) + 1 );
  eventParams.insert( "days_used", days_used );

  // Create GA4 event object
  QJsonObject eventObj;
  eventObj.insert( "name", "app_start" );
  eventObj.insert( "params", eventParams );

  QJsonArray eventsArray;
  eventsArray.append( eventObj );

  // Construct final JSON payload
  QJsonObject mainPayload;
  QString app_uuid = Settings::instance().applicationUuid();
  // remove {} from uuid if needed
  app_uuid.remove( QRegularExpression( "[{}]" ) );
  mainPayload.insert( "client_id", app_uuid );
  mainPayload.insert( "events", eventsArray );

  QByteArray json_data = QJsonDocument( mainPayload ).toJson( QJsonDocument::Compact );

#ifdef BEEBEEP_DEBUG
  qDebug() << "Posting anonymous data to GA4:" << json_data;
#endif

  QNetworkReply* reply = mp_manager->post( req, json_data );
#ifndef QT_NO_SSL
  connect( reply, SIGNAL( sslErrors( const QList<QSslError>& ) ), this, SLOT( onSslErrors( const QList<QSslError>& ) ) );
#endif
}


void GAnalytics::onReplyFinished( QNetworkReply *reply )
{
#ifdef BEEBEEP_DEBUG
  int statusCode = reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();

  if( reply->error() != QNetworkReply::NoError )
  {
    qWarning() << qPrintable( objectName() ) << "has error:" << reply->errorString()
               << "- HTTP status:" << statusCode;

    // Read response body (useful if using the /debug/mp/collect endpoint to inspect validation errors)
    QByteArray responseData = reply->readAll();
    if( !responseData.isEmpty() )
      qDebug() << "GA4 validation response:" << responseData;
  }
  else
  {
    qDebug() << qPrintable( objectName() ) << "has finished successfully with HTTP status:" << statusCode;
  }
#endif

  // GA4 returns HTTP 204 (No Content) or 200 on success
  if( reply->error() == QNetworkReply::NoError )
  {
    Settings::instance().setStatsPostDate( QDate::currentDate() );
  }

  // Clean up resources and signal completion
  reply->deleteLater();
  emit jobFinished();
}

void GAnalytics::onSslErrors( const QList<QSslError>& ssl_errors )
{
#ifndef QT_NO_SSL
  foreach( QSslError ssl_error, ssl_errors )
    qWarning() << qPrintable( objectName() ) << "has found SSL error:" << qPrintable( ssl_error.errorString() );
#else
  Q_UNUSED( ssl_errors );
#endif
}

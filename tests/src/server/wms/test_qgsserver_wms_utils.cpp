/***************************************************************************
     test_qgsserver_wms_utils.cpp
     ----------------------------
    Date                 : November 2026
    Copyright            : (C) 2026 by Michal Wloga
    Email                : michal dot wloga at envirosolutions dot pl
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgstest.h"
#include "qgsbufferserverresponse.h"
#include "qgswmsutils.h"

#include <QBuffer>
#include <QImage>
#include <QPainter>

/**
 * \ingroup UnitTests
 * This is a unit test for the WMS utilities
 */
class TestQgsServerWmsUtils : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();

    void writeImage_png_data();
    void writeImage_png();
};

void TestQgsServerWmsUtils::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();
}

void TestQgsServerWmsUtils::cleanupTestCase()
{
  QgsApplication::exitQgis();
}

void TestQgsServerWmsUtils::writeImage_png_data()
{
  QTest::addColumn<int>( "format" );
  QTest::addColumn<int>( "width" );
  QTest::addColumn<int>( "height" );

  QTest::newRow( "opaque" ) << static_cast<int>( QImage::Format_RGB32 ) << 301 << 157;
  QTest::newRow( "alpha" ) << static_cast<int>( QImage::Format_ARGB32 ) << 301 << 157;
  QTest::newRow( "premultiplied alpha" ) << static_cast<int>( QImage::Format_ARGB32_Premultiplied ) << 301 << 157;
  QTest::newRow( "one pixel" ) << static_cast<int>( QImage::Format_ARGB32_Premultiplied ) << 1 << 1;
}

void TestQgsServerWmsUtils::writeImage_png()
{
  QFETCH( int, format );
  QFETCH( int, width );
  QFETCH( int, height );

  // an image with flat areas, gradients, antialiased edges and partial transparency
  QImage image( width, height, static_cast<QImage::Format>( format ) );
  image.fill( QColor( 255, 255, 255, 0 ) );
  {
    QPainter painter( &image );
    painter.setRenderHint( QPainter::Antialiasing );
    QLinearGradient gradient( 0, 0, width, height );
    gradient.setColorAt( 0, QColor( 200, 30, 40, 255 ) );
    gradient.setColorAt( 1, QColor( 20, 90, 220, 60 ) );
    painter.fillRect( QRectF( 0, 0, width / 2.0, height ), gradient );
    painter.setPen( QPen( QColor( 10, 120, 30, 180 ), 2.5 ) );
    painter.drawLine( QPointF( 0.3, 0.7 ), QPointF( width - 0.4, height - 1.2 ) );
    painter.setBrush( QColor( 250, 200, 0, 128 ) );
    painter.drawEllipse( QRectF( width / 3.0, height / 4.0, width / 2.0, height / 2.0 ) );
  }
  image.setDotsPerMeterX( 3780 );
  image.setDotsPerMeterY( 3543 );

  QgsBufferServerResponse response;
  QImage toWrite = image;
  QgsWms::writeImage( response, toWrite, QStringLiteral( "image/png" ) );
  QCOMPARE( response.header( QStringLiteral( "Content-Type" ) ), QStringLiteral( "image/png" ) );

  response.flush();
  const QByteArray data = response.body();
  QVERIFY( data.startsWith( "\x89PNG" ) );
  const QImage decoded = QImage::fromData( data, "PNG" );
  QVERIFY( !decoded.isNull() );

  // the result is the same as with the PNG writer of Qt
  QBuffer buffer;
  buffer.open( QIODevice::WriteOnly );
  QVERIFY( image.save( &buffer, "PNG" ) );
  const QImage expected = QImage::fromData( buffer.data(), "PNG" );

  QCOMPARE( decoded.size(), expected.size() );
  QCOMPARE( decoded.format(), expected.format() );
  QCOMPARE( decoded.dotsPerMeterX(), expected.dotsPerMeterX() );
  QCOMPARE( decoded.dotsPerMeterY(), expected.dotsPerMeterY() );
  QVERIFY( decoded == expected );
}

QGSTEST_MAIN( TestQgsServerWmsUtils )
#include "test_qgsserver_wms_utils.moc"

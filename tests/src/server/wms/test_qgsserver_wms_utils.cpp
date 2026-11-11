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
#include "qgsmediancut.h"
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
    void imageWithColorTable_data();
    void imageWithColorTable();
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
  QTest::addColumn<bool>( "continuousTone" );

  QTest::newRow( "opaque" ) << static_cast<int>( QImage::Format_RGB32 ) << 301 << 157 << false;
  QTest::newRow( "alpha" ) << static_cast<int>( QImage::Format_ARGB32 ) << 301 << 157 << false;
  QTest::newRow( "premultiplied alpha" ) << static_cast<int>( QImage::Format_ARGB32_Premultiplied ) << 301 << 157 << false;
  QTest::newRow( "one pixel" ) << static_cast<int>( QImage::Format_ARGB32_Premultiplied ) << 1 << 1 << false;
  // images without flat areas are written with another row filter
  QTest::newRow( "opaque continuous tone" ) << static_cast<int>( QImage::Format_RGB32 ) << 301 << 157 << true;
  QTest::newRow( "premultiplied alpha continuous tone" ) << static_cast<int>( QImage::Format_ARGB32_Premultiplied ) << 301 << 157 << true;
}

void TestQgsServerWmsUtils::writeImage_png()
{
  QFETCH( int, format );
  QFETCH( int, width );
  QFETCH( int, height );
  QFETCH( bool, continuousTone );

  // an image with flat areas, gradients, antialiased edges and partial transparency
  QImage image( width, height, static_cast<QImage::Format>( format ) );
  image.fill( QColor( 255, 255, 255, 0 ) );
  if ( continuousTone )
  {
    // every pixel differs from its neighbours
    for ( int y = 0; y < height; ++y )
    {
      for ( int x = 0; x < width; ++x )
        image.setPixelColor( x, y, QColor( ( x * 7 + y * 3 ) % 256, ( x * 5 + y * 11 ) % 256, ( x * 13 + y ) % 256, 255 - ( x + y ) % 100 ) );
    }
  }
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

void TestQgsServerWmsUtils::imageWithColorTable_data()
{
  QTest::addColumn<int>( "format" );
  QTest::addColumn<int>( "colors" );

  QTest::newRow( "256 colors" ) << static_cast<int>( QImage::Format_ARGB32 ) << 256;
  QTest::newRow( "16 colors" ) << static_cast<int>( QImage::Format_ARGB32 ) << 16;
  QTest::newRow( "1 color" ) << static_cast<int>( QImage::Format_ARGB32 ) << 1;
  QTest::newRow( "opaque" ) << static_cast<int>( QImage::Format_RGB32 ) << 256;
  QTest::newRow( "premultiplied alpha" ) << static_cast<int>( QImage::Format_ARGB32_Premultiplied ) << 256;
}

void TestQgsServerWmsUtils::imageWithColorTable()
{
  QFETCH( int, format );
  QFETCH( int, colors );

  // flat areas, a gradient, antialiased edges, partial transparency and an area where every pixel differs
  const int width = 211;
  const int height = 157;
  QImage image( width, height, static_cast<QImage::Format>( format ) );
  image.fill( QColor( 255, 255, 255, 0 ) );
  for ( int y = 0; y < height / 3; ++y )
  {
    for ( int x = 0; x < width; ++x )
      image.setPixelColor( x, y, QColor( ( x * 7 + y * 3 ) % 256, ( x * 5 + y * 11 ) % 256, ( x * 13 + y ) % 256, 255 - ( x + y ) % 100 ) );
  }
  {
    QPainter painter( &image );
    painter.setRenderHint( QPainter::Antialiasing );
    QLinearGradient gradient( 0, 0, width, height );
    gradient.setColorAt( 0, QColor( 200, 30, 40, 255 ) );
    gradient.setColorAt( 1, QColor( 20, 90, 220, 60 ) );
    painter.fillRect( QRectF( 0, height / 2.0, width / 2.0, height / 2.0 ), gradient );
    painter.setBrush( QColor( 250, 200, 0, 128 ) );
    painter.drawEllipse( QRectF( width / 3.0, height / 4.0, width / 2.0, height / 2.0 ) );
  }
  image.setDotsPerMeterX( 3780 );
  image.setDotsPerMeterY( 3543 );
  image.setText( QStringLiteral( "Title" ), QStringLiteral( "test" ) );

  QVector<QRgb> colorTable;
  QgsWms::medianCut( colorTable, colors, image );
  QVERIFY( !colorTable.isEmpty() );
  QVERIFY( colorTable.size() <= colors );
  // colors at the same distance of a pixel: the first one of the table has to be used
  if ( colorTable.size() > 2 )
    colorTable[colorTable.size() - 1] = colorTable.at( 0 );

  const QImage expected = image.convertToFormat( QImage::Format_Indexed8, colorTable, Qt::ColorOnly | Qt::ThresholdDither | Qt::ThresholdAlphaDither | Qt::NoOpaqueDetection );
  const QImage result = QgsWms::imageWithColorTable( image, colorTable );

  QCOMPARE( result.format(), QImage::Format_Indexed8 );
  QCOMPARE( result.size(), expected.size() );
  QCOMPARE( result.colorTable(), expected.colorTable() );
  QCOMPARE( result.dotsPerMeterX(), expected.dotsPerMeterX() );
  QCOMPARE( result.dotsPerMeterY(), expected.dotsPerMeterY() );
  QCOMPARE( result.textKeys(), expected.textKeys() );
  for ( int y = 0; y < height; ++y )
    QVERIFY2( memcmp( result.constScanLine( y ), expected.constScanLine( y ), width ) == 0, qPrintable( QStringLiteral( "row %1" ).arg( y ) ) );

  // the files written from both images are the same
  QBuffer resultBuffer;
  resultBuffer.open( QIODevice::WriteOnly );
  QVERIFY( result.save( &resultBuffer, "PNG" ) );
  QBuffer expectedBuffer;
  expectedBuffer.open( QIODevice::WriteOnly );
  QVERIFY( expected.save( &expectedBuffer, "PNG" ) );
  QCOMPARE( resultBuffer.data(), expectedBuffer.data() );
}

QGSTEST_MAIN( TestQgsServerWmsUtils )
#include "test_qgsserver_wms_utils.moc"

/***************************************************************************
                              qgswmsutils.cpp
                              -------------------------
  begin                : December 20 , 2016
  copyright            : (C) 2007 by Marco Hugentobler  ( parts from qgswmshandler)
                         (C) 2014 by Alessandro Pasotti ( parts from qgswmshandler)
                         (C) 2016 by David Marteau
  email                : marco dot hugentobler at karto dot baug dot ethz dot ch
                         a dot pasotti at itopen dot it
                         david dot marteau at 3liz dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                  *
 *                                                                         *
 ***************************************************************************/

#include <QRegularExpression>

#include <algorithm>
#include <png.h>

#include "qgsmodule.h"
#include "qgswmsutils.h"
#include "qgsmediancut.h"
#include "qgsserverprojectutils.h"
#include "qgswmsserviceexception.h"
#include "qgsproject.h"

namespace
{
  void pngWriteCallback( png_structp png, png_bytep data, png_size_t length )
  {
    QIODevice *device = static_cast<QIODevice *>( png_get_io_ptr( png ) );
    const qint64 size = static_cast<qint64>( length );
    if ( device->write( reinterpret_cast<const char *>( data ), size ) != size )
      png_error( png, "Write error" );
  }

  void pngFlushCallback( png_structp )
  {
  }

  /**
   * Returns the PNG row filter to use for a 32 bits \a image.
   *
   * Rendered vector maps are mostly made of flat areas, which deflate better and faster
   * without row filtering. Continuous tone images, like aerial imagery, are both smaller
   * and faster to deflate with a row filter. A few rows are sampled to tell them apart.
   */
  int pngRowFilter( const QImage &image )
  {
    const int width = image.width();
    const int height = image.height();
    const int step = std::max( 1, height / 16 );
    qint64 flat = 0;
    qint64 total = 0;
    for ( int y = 0; y < height; y += step )
    {
      const QRgb *row = reinterpret_cast<const QRgb *>( image.constScanLine( y ) );
      for ( int x = 1; x < width; ++x )
      {
        if ( row[x] == row[x - 1] )
          ++flat;
      }
      total += width - 1;
    }
    return flat * 3 < total ? PNG_FILTER_UP : PNG_FILTER_NONE;
  }

  /**
   * Writes a 32 bits \a image as PNG to \a device, with a single row filter and
   * a low deflate level.
   *
   * For rendered maps this is several times faster than the PNG writer of Qt, which
   * uses the adaptive row filters and the default deflate level, and the result is
   * usually not larger. The decoded pixels are the same.
   *
   * Returns FALSE if the image could not be written, in particular when its format
   * is not handled.
   */
  bool writeFastPng( QIODevice *device, const QImage &img )
  {
    QImage image = img;
    bool hasAlpha = false;
    switch ( image.format() )
    {
      case QImage::Format_RGB32:
        break;
      case QImage::Format_ARGB32:
        hasAlpha = true;
        break;
      case QImage::Format_ARGB32_Premultiplied:
        // same conversion as the one done by the PNG writer of Qt
        image = image.convertToFormat( QImage::Format_ARGB32 );
        hasAlpha = true;
        break;
      default:
        return false;
    }

    const int rowFilter = pngRowFilter( image );

    png_structp png = png_create_write_struct( PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr );
    if ( !png )
      return false;

    png_infop info = png_create_info_struct( png );
    if ( !info )
    {
      png_destroy_write_struct( &png, nullptr );
      return false;
    }

    // libpng jumps here on error
    if ( setjmp( png_jmpbuf( png ) ) )
    {
      png_destroy_write_struct( &png, &info );
      return false;
    }

    png_set_write_fn( png, device, pngWriteCallback, pngFlushCallback );
    png_set_compression_level( png, 2 );
    png_set_filter( png, PNG_FILTER_TYPE_BASE, rowFilter );
    png_set_IHDR( png, info, static_cast<png_uint_32>( image.width() ), static_cast<png_uint_32>( image.height() ), 8, hasAlpha ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT );
    if ( image.dotsPerMeterX() > 0 || image.dotsPerMeterY() > 0 )
      png_set_pHYs( png, info, static_cast<png_uint_32>( image.dotsPerMeterX() ), static_cast<png_uint_32>( image.dotsPerMeterY() ), PNG_RESOLUTION_METER );
    png_write_info( png, info );

    // pixels are stored as 0xAARRGGBB in the byte order of the host
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    png_set_bgr( png );
    if ( !hasAlpha )
      png_set_filler( png, 0, PNG_FILLER_AFTER );
#else
    if ( hasAlpha )
      png_set_swap_alpha( png );
    else
      png_set_filler( png, 0, PNG_FILLER_BEFORE );
#endif

    const int height = image.height();
    for ( int y = 0; y < height; ++y )
      png_write_row( png, image.constScanLine( y ) );

    png_write_end( png, info );
    png_destroy_write_struct( &png, &info );
    return true;
  }
} // namespace

namespace QgsWms
{
  QString implementationVersion()
  {
    return QStringLiteral( "1.3.0" );
  }

  QUrl serviceUrl( const QgsServerRequest &request, const QgsProject *project, const QgsServerSettings &settings )
  {
    QUrl href;
    href.setUrl( QgsServerProjectUtils::wmsServiceUrl( project ? *project : *QgsProject::instance(), request, settings ) );

    // Build default url
    if ( href.isEmpty() )
    {
      static const QSet<QString> sFilter {
        QStringLiteral( "REQUEST" ),
        QStringLiteral( "VERSION" ),
        QStringLiteral( "SERVICE" ),
        QStringLiteral( "LAYERS" ),
        QStringLiteral( "STYLES" ),
        QStringLiteral( "SLD_VERSION" ),
        QStringLiteral( "_DC" )
      };

      href = request.originalUrl();
      QUrlQuery q( href );

      const QList<QPair<QString, QString>> queryItems = q.queryItems();
      for ( const QPair<QString, QString> &param : queryItems )
      {
        if ( sFilter.contains( param.first.toUpper() ) )
          q.removeAllQueryItems( param.first );
      }

      href.setQuery( q );
    }

    return href;
  }


  ImageOutputFormat parseImageFormat( const QString &format )
  {
    if ( format.compare( QLatin1String( "png" ), Qt::CaseInsensitive ) == 0 || format.compare( QLatin1String( "image/png" ), Qt::CaseInsensitive ) == 0 )
    {
      return ImageOutputFormat::PNG;
    }
    else if ( format.compare( QLatin1String( "jpg " ), Qt::CaseInsensitive ) == 0 || format.compare( QLatin1String( "image/jpeg" ), Qt::CaseInsensitive ) == 0 )
    {
      return ImageOutputFormat::JPEG;
    }
    else if ( format.compare( QLatin1String( "webp" ), Qt::CaseInsensitive ) == 0 || format.compare( QLatin1String( "image/webp" ), Qt::CaseInsensitive ) == 0 )
    {
      return ImageOutputFormat::WEBP;
    }
    else
    {
      // lookup for png with mode
      const thread_local QRegularExpression modeExpr = QRegularExpression( QStringLiteral( "image/png\\s*;\\s*mode=([^;]+)" ), QRegularExpression::CaseInsensitiveOption );

      const QRegularExpressionMatch match = modeExpr.match( format );
      const QString mode = match.captured( 1 );
      if ( mode.compare( QLatin1String( "16bit" ), Qt::CaseInsensitive ) == 0 )
        return ImageOutputFormat::PNG16;
      if ( mode.compare( QLatin1String( "8bit" ), Qt::CaseInsensitive ) == 0 )
        return ImageOutputFormat::PNG8;
      if ( mode.compare( QLatin1String( "1bit" ), Qt::CaseInsensitive ) == 0 )
        return ImageOutputFormat::PNG1;
    }

    return ImageOutputFormat::Unknown;
  }

  // Write image response
  void writeImage( QgsServerResponse &response, QImage &img, const QString &formatStr, int imageQuality )
  {
    const ImageOutputFormat outputFormat = parseImageFormat( formatStr );
    QImage result;
    QString saveFormat;
    QString contentType;
    switch ( outputFormat )
    {
      case ImageOutputFormat::PNG:
        result = img;
        contentType = QStringLiteral( "image/png" );
        saveFormat = QStringLiteral( "PNG" );
        break;
      case ImageOutputFormat::PNG8:
      {
        QVector<QRgb> colorTable;

        // Rendering is made with the format QImage::Format_ARGB32_Premultiplied
        // So we need to convert it in QImage::Format_ARGB32 in order to properly build
        // the color table.
        const QImage img256 = img.convertToFormat( QImage::Format_ARGB32 );
        medianCut( colorTable, 256, img256 );
        result = img256.convertToFormat( QImage::Format_Indexed8, colorTable, Qt::ColorOnly | Qt::ThresholdDither | Qt::ThresholdAlphaDither | Qt::NoOpaqueDetection );
      }
        contentType = QStringLiteral( "image/png" );
        saveFormat = QStringLiteral( "PNG" );
        break;
      case ImageOutputFormat::PNG16:
        result = img.convertToFormat( QImage::Format_ARGB4444_Premultiplied );
        contentType = QStringLiteral( "image/png" );
        saveFormat = QStringLiteral( "PNG" );
        break;
      case ImageOutputFormat::PNG1:
        result = img.convertToFormat( QImage::Format_Mono, Qt::MonoOnly | Qt::ThresholdDither | Qt::ThresholdAlphaDither | Qt::NoOpaqueDetection );
        contentType = QStringLiteral( "image/png" );
        saveFormat = QStringLiteral( "PNG" );
        break;
      case ImageOutputFormat::JPEG:
        result = img;
        contentType = QStringLiteral( "image/jpeg" );
        saveFormat = QStringLiteral( "JPEG" );
        break;
      case ImageOutputFormat::WEBP:
        result = img;
        contentType = QStringLiteral( "image/webp" );
        saveFormat = QStringLiteral( "WEBP" );
        break;
      case ImageOutputFormat::Unknown:
        QgsMessageLog::logMessage( QStringLiteral( "Unsupported format string %1" ).arg( formatStr ) );
        saveFormat = QStringLiteral( "Unknown" );
        break;
    }

    // Preserve DPI, some conversions, in particular the one for 8bit will drop this information
    result.setDotsPerMeterX( img.dotsPerMeterX() );
    result.setDotsPerMeterY( img.dotsPerMeterY() );

    if ( outputFormat != ImageOutputFormat::Unknown )
    {
      response.setHeader( "Content-Type", contentType );
      if ( saveFormat == QLatin1String( "JPEG" ) || saveFormat == QLatin1String( "WEBP" ) )
      {
        result.save( response.io(), qPrintable( saveFormat ), imageQuality );
      }
      else if ( outputFormat != ImageOutputFormat::PNG || !writeFastPng( response.io(), result ) )
      {
        result.save( response.io(), qPrintable( saveFormat ) );
      }
    }
    else
    {
      QgsWmsParameter parameter( QgsWmsParameter::FORMAT );
      parameter.mValue = formatStr;
      throw QgsBadRequestException( QgsServiceException::OGC_InvalidFormat, parameter );
    }
  }
} // namespace QgsWms

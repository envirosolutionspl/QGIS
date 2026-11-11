/***************************************************************************
                              qgsmediancut.cpp
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

#include "qgsmediancut.h"

#include <QList>
#include <QMultiMap>
#include <QHash>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>

namespace QgsWms
{

  typedef QVector<QPair<QRgb, int>> QgsColorBox;      //Color / number of pixels
  typedef QMultiMap<int, QgsColorBox> QgsColorBoxMap; // sum of pixels / color box

  namespace
  {

    void imageColors( QHash<QRgb, int> &colors, const QImage &image )
    {
      colors.clear();
      const int width = image.width();
      const int height = image.height();

      const QRgb *currentScanLine = nullptr;
      QHash<QRgb, int>::iterator colorIt;
      for ( int i = 0; i < height; ++i )
      {
        currentScanLine = ( const QRgb * ) ( image.scanLine( i ) );
        for ( int j = 0; j < width; ++j )
        {
          colorIt = colors.find( currentScanLine[j] );
          if ( colorIt == colors.end() )
          {
            colors.insert( currentScanLine[j], 1 );
          }
          else
          {
            colorIt.value()++;
          }
        }
      }
    }

    bool minMaxRange( const QgsColorBox &colorBox, int &redRange, int &greenRange, int &blueRange, int &alphaRange )
    {
      if ( colorBox.size() < 1 )
      {
        return false;
      }

      int rMin = std::numeric_limits<int>::max();
      int gMin = std::numeric_limits<int>::max();
      int bMin = std::numeric_limits<int>::max();
      int aMin = std::numeric_limits<int>::max();
      int rMax = std::numeric_limits<int>::min();
      int gMax = std::numeric_limits<int>::min();
      int bMax = std::numeric_limits<int>::min();
      int aMax = std::numeric_limits<int>::min();

      int currentRed = 0;
      int currentGreen = 0;
      int currentBlue = 0;
      int currentAlpha = 0;

      for ( auto colorBoxIt = colorBox.constBegin(); colorBoxIt != colorBox.constEnd(); ++colorBoxIt )
      {
        currentRed = qRed( colorBoxIt->first );
        if ( currentRed > rMax )
        {
          rMax = currentRed;
        }
        if ( currentRed < rMin )
        {
          rMin = currentRed;
        }

        currentGreen = qGreen( colorBoxIt->first );
        if ( currentGreen > gMax )
        {
          gMax = currentGreen;
        }
        if ( currentGreen < gMin )
        {
          gMin = currentGreen;
        }

        currentBlue = qBlue( colorBoxIt->first );
        if ( currentBlue > bMax )
        {
          bMax = currentBlue;
        }
        if ( currentBlue < bMin )
        {
          bMin = currentBlue;
        }

        currentAlpha = qAlpha( colorBoxIt->first );
        if ( currentAlpha > aMax )
        {
          aMax = currentAlpha;
        }
        if ( currentAlpha < aMin )
        {
          aMin = currentAlpha;
        }
      }

      redRange = rMax - rMin;
      greenRange = gMax - gMin;
      blueRange = bMax - bMin;
      alphaRange = aMax - aMin;
      return true;
    }

    QRgb boxColor( const QgsColorBox &box, int boxPixels )
    {
      double avRed = 0;
      double avGreen = 0;
      double avBlue = 0;
      double avAlpha = 0;
      QRgb currentColor;
      int currentPixel;

      double weight;

      for ( auto colorBoxIt = box.constBegin(); colorBoxIt != box.constEnd(); ++colorBoxIt )
      {
        currentColor = colorBoxIt->first;
        currentPixel = colorBoxIt->second;
        weight = static_cast<double>( currentPixel ) / boxPixels;
        avRed += ( qRed( currentColor ) * weight );
        avGreen += ( qGreen( currentColor ) * weight );
        avBlue += ( qBlue( currentColor ) * weight );
        avAlpha += ( qAlpha( currentColor ) * weight );
      }

      return qRgba( avRed, avGreen, avBlue, avAlpha );
    }


    void splitColorBox( QgsColorBox &colorBox, QgsColorBoxMap &colorBoxMap, QMultiMap<int, QgsColorBox>::iterator colorBoxMapIt )
    {
      if ( colorBox.size() < 2 )
      {
        return; //need at least two colors for a split
      }

      //a,r,g,b ranges
      int redRange = 0;
      int greenRange = 0;
      int blueRange = 0;
      int alphaRange = 0;

      if ( !minMaxRange( colorBox, redRange, greenRange, blueRange, alphaRange ) )
      {
        return;
      }

      //sort color box for a/r/g/b (with lambdas: a comparison function passed as a pointer is not inlined in the sort)
      if ( redRange >= greenRange && redRange >= blueRange && redRange >= alphaRange )
      {
        std::sort( colorBox.begin(), colorBox.end(), []( const QPair<QRgb, int> &c1, const QPair<QRgb, int> &c2 ) { return qRed( c1.first ) < qRed( c2.first ); } );
      }
      else if ( greenRange >= redRange && greenRange >= blueRange && greenRange >= alphaRange )
      {
        std::sort( colorBox.begin(), colorBox.end(), []( const QPair<QRgb, int> &c1, const QPair<QRgb, int> &c2 ) { return qGreen( c1.first ) < qGreen( c2.first ); } );
      }
      else if ( blueRange >= redRange && blueRange >= greenRange && blueRange >= alphaRange )
      {
        std::sort( colorBox.begin(), colorBox.end(), []( const QPair<QRgb, int> &c1, const QPair<QRgb, int> &c2 ) { return qBlue( c1.first ) < qBlue( c2.first ); } );
      }
      else
      {
        std::sort( colorBox.begin(), colorBox.end(), []( const QPair<QRgb, int> &c1, const QPair<QRgb, int> &c2 ) { return qAlpha( c1.first ) < qAlpha( c2.first ); } );
      }

      //get median
      const double halfSum = colorBoxMapIt.key() / 2.0;
      int currentSum = 0;
      int currentListIndex = 0;

      auto colorBoxIt = colorBox.begin();
      for ( ; colorBoxIt != colorBox.end(); ++colorBoxIt )
      {
        currentSum += colorBoxIt->second;
        if ( currentSum >= halfSum )
        {
          break;
        }
        ++currentListIndex;
      }

      if ( currentListIndex > ( colorBox.size() - 2 ) ) //if the median is contained in the last color, split one item before that
      {
        --currentListIndex;
        currentSum -= colorBoxIt->second;
      }
      else
      {
        ++colorBoxIt; //the iterator needs to point behind the last item to remove
      }

      //do split: replace old color box, insert new one
      const QgsColorBox newColorBox1 = colorBox.mid( 0, currentListIndex + 1 );
      colorBoxMap.insert( currentSum, newColorBox1 );

      colorBox.erase( colorBox.begin(), colorBoxIt );
      const QgsColorBox newColorBox2 = colorBox;
      colorBoxMap.erase( colorBoxMapIt );
      colorBoxMap.insert( halfSum * 2.0 - currentSum, newColorBox2 );
    }

  } // namespace

  void medianCut( QVector<QRgb> &colorTable, int nColors, const QImage &inputImage )
  {
    QHash<QRgb, int> inputColors;
    imageColors( inputColors, inputImage );

    if ( inputColors.size() <= nColors ) //all the colors in the image can be mapped to one palette color
    {
      colorTable.resize( inputColors.size() );
      int index = 0;
      for ( auto inputColorIt = inputColors.constBegin(); inputColorIt != inputColors.constEnd(); ++inputColorIt )
      {
        colorTable[index] = inputColorIt.key();
        ++index;
      }
      return;
    }

    //create first box
    QgsColorBox firstBox; //QList< QPair<QRgb, int> >
    int firstBoxPixelSum = 0;
    for ( auto inputColorIt = inputColors.constBegin(); inputColorIt != inputColors.constEnd(); ++inputColorIt )
    {
      firstBox.push_back( qMakePair( inputColorIt.key(), inputColorIt.value() ) );
      firstBoxPixelSum += inputColorIt.value();
    }

    QgsColorBoxMap colorBoxMap; //QMultiMap< int, ColorBox >
    colorBoxMap.insert( firstBoxPixelSum, firstBox );
    QMultiMap<int, QgsColorBox>::iterator colorBoxMapIt = colorBoxMap.end();

    //split boxes until number of boxes == nColors or all the boxes have color count 1
    bool allColorsMapped = false;
    while ( colorBoxMap.size() < nColors )
    {
      //start at the end of colorBoxMap and pick the first entry with number of colors < 1
      colorBoxMapIt = colorBoxMap.end();
      while ( true )
      {
        --colorBoxMapIt;
        if ( colorBoxMapIt.value().size() > 1 )
        {
          splitColorBox( colorBoxMapIt.value(), colorBoxMap, colorBoxMapIt );
          break;
        }
        if ( colorBoxMapIt == colorBoxMap.begin() )
        {
          allColorsMapped = true;
          break;
        }
      }

      if ( allColorsMapped )
      {
        break;
      }
    }

    //get representative colors for the boxes
    int index = 0;
    colorTable.resize( colorBoxMap.size() );
    for ( auto colorBoxIt = colorBoxMap.constBegin(); colorBoxIt != colorBoxMap.constEnd(); ++colorBoxIt )
    {
      colorTable[index] = boxColor( colorBoxIt.value(), colorBoxIt.key() );
      ++index;
    }
  }

  QImage imageWithColorTable( const QImage &image, const QVector<QRgb> &colorTable )
  {
    const Qt::ImageConversionFlags flags = Qt::ColorOnly | Qt::ThresholdDither | Qt::ThresholdAlphaDither | Qt::NoOpaqueDetection;
    const int nColors = colorTable.size();
    QImage result;
    // Qt converts the images with another format to QImage::Format_ARGB32 first, this is left to it
    if ( image.format() == QImage::Format_ARGB32 && nColors > 0 && nColors <= 256 )
      result = QImage( image.size(), QImage::Format_Indexed8 );
    if ( result.isNull() )
      return image.convertToFormat( QImage::Format_Indexed8, colorTable, flags );

    // the color table, the resolution and the texts of the result are the ones set by Qt for this conversion
    const QImage reference = image.copy( 0, 0, 1, 1 ).convertToFormat( QImage::Format_Indexed8, colorTable, flags );
    result.setColorTable( reference.colorTable() );
    result.setDotsPerMeterX( reference.dotsPerMeterX() );
    result.setDotsPerMeterY( reference.dotsPerMeterY() );
    result.setDevicePixelRatio( reference.devicePixelRatio() );
    const QStringList textKeys = reference.textKeys();
    for ( const QString &key : textKeys )
      result.setText( key, reference.text( key ) );

    // one array for each channel, so that the distances to all the colors of the table are calculated with SIMD instructions
    quint8 red[256], green[256], blue[256], alpha[256];
    for ( int i = 0; i < nColors; ++i )
    {
      red[i] = static_cast<quint8>( qRed( colorTable.at( i ) ) );
      green[i] = static_cast<quint8>( qGreen( colorTable.at( i ) ) );
      blue[i] = static_cast<quint8>( qBlue( colorTable.at( i ) ) );
      alpha[i] = static_cast<quint8>( qAlpha( colorTable.at( i ) ) );
    }
    quint16 distances[256];

    // Direct mapped cache of the closest color of the colors already found in the image (maps have many pixels
    // of the same color): a color replaces the color which was in its entry
    struct CacheEntry
    {
        QRgb pixel;
        int index; // in the color table, -1 for an empty entry
    };
    std::vector<CacheEntry> cache( 1 << 16, CacheEntry { 0, -1 } );
    const int width = image.width();
    const int height = image.height();
    for ( int y = 0; y < height; ++y )
    {
      const QRgb *pixels = reinterpret_cast<const QRgb *>( image.constScanLine( y ) );
      uchar *indexes = result.scanLine( y );
      for ( int x = 0; x < width; ++x )
      {
        const QRgb pixel = pixels[x];
        CacheEntry &entry = cache[( pixel * 2654435761U ) >> 16];
        if ( entry.index < 0 || entry.pixel != pixel )
        {
          // same distance as in Qt: sum of the absolute differences of the channels
          const int r = qRed( pixel );
          const int g = qGreen( pixel );
          const int b = qBlue( pixel );
          const int a = qAlpha( pixel );
          quint16 smallest = std::numeric_limits<quint16>::max();
          for ( int i = 0; i < nColors; ++i )
          {
            distances[i] = static_cast<quint16>( std::abs( r - red[i] ) + std::abs( g - green[i] ) + std::abs( b - blue[i] ) + std::abs( a - alpha[i] ) );
            smallest = std::min( smallest, distances[i] );
          }
          // first color of the table with the smallest distance, as in Qt
          entry.pixel = pixel;
          entry.index = static_cast<int>( std::find( distances, distances + nColors, smallest ) - distances );
        }
        indexes[x] = static_cast<uchar>( entry.index );
      }
    }
    return result;
  }

} // namespace QgsWms

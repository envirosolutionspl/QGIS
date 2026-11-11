/***************************************************************************
                              qgswfssutils.cpp
                              -------------------------
  begin                : December 20 , 2016
  copyright            : (C) 2007 by Marco Hugentobler  ( parts from qgswmshandler)
                         (C) 2012 by René-Luc D'Hont    ( parts from qgswmshandler)
                         (C) 2014 by Alessandro Pasotti ( parts from qgswmshandler)
                         (C) 2017 by David Marteau
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

#include "qgswfsutils.h"
#include "qgsogcutils.h"
#include "qgsserverprojectutils.h"
#include "qgsvectorlayer.h"
#include "qgsproject.h"

#include <QDomDocument>
#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
#include <QTextCodec>
#include <QTextStream>

namespace
{

  /**
   * Text codec which only answers QTextCodec::canEncode() for a document written to a string as UTF-8:
   * the characters which are always valid in UTF-8 are accepted without a conversion, the decision for
   * the other ones (surrogates, non characters) is left to the UTF-8 codec of Qt.
   */
  class Utf8CheckCodec : public QTextCodec
  {
    public:
      Utf8CheckCodec()
        : mUtf8( QTextCodec::codecForName( "UTF-8" ) )
      {}

      QByteArray name() const override { return QByteArrayLiteral( "x-qgis-utf8-check" ); }
      int mibEnum() const override { return -1001; } // not an IANA character set

    protected:
      QString convertToUnicode( const char *chars, int length, ConverterState *state ) const override
      {
        return mUtf8->toUnicode( chars, length, state );
      }

      QByteArray convertFromUnicode( const QChar *chars, int length, ConverterState *state ) const override
      {
        for ( int i = 0; i < length; ++i )
        {
          const ushort c = chars[i].unicode();
          if ( c >= 0xD800 && ( c < 0xE000 || c >= 0xFDD0 ) )
            return mUtf8->fromUnicode( chars, length, state );
        }
        return QByteArray();
      }

    private:
      QTextCodec *mUtf8 = nullptr;
  };

} // namespace
#endif

namespace QgsWfs
{
  QString implementationVersion()
  {
    return QStringLiteral( "1.1.0" );
  }

  QString serviceUrl( const QgsServerRequest &request, const QgsProject *project, const QgsServerSettings &settings )
  {
    QUrl href;
    href.setUrl( QgsServerProjectUtils::wfsServiceUrl( project ? *project : *QgsProject::instance(), request, settings ) );

    // Build default url
    if ( href.isEmpty() )
    {
      static QSet<QString> sFilter {
        QStringLiteral( "REQUEST" ),
        QStringLiteral( "VERSION" ),
        QStringLiteral( "SERVICE" ),
      };

      href = request.originalUrl();
      QUrlQuery q( href );

      const auto constQueryItems = q.queryItems();
      for ( const auto &param : constQueryItems )
      {
        if ( sFilter.contains( param.first.toUpper() ) )
          q.removeAllQueryItems( param.first );
      }

      href.setQuery( q );
    }

    return href.toString();
  }

  QString layerTypeName( const QgsMapLayer *layer )
  {
    QString name = layer->name();
    if ( !layer->serverProperties()->shortName().isEmpty() )
      name = layer->serverProperties()->shortName();
    name = name.replace( ' ', '_' ).replace( ':', '-' );
    return name;
  }

  QgsVectorLayer *layerByTypeName( const QgsProject *project, const QString &typeName )
  {
    QStringList layerIds = QgsServerProjectUtils::wfsLayerIds( *project );
    for ( const QString &layerId : std::as_const( layerIds ) )
    {
      QgsMapLayer *layer = project->mapLayer( layerId );
      if ( !layer )
      {
        continue;
      }
      if ( layer->type() != Qgis::LayerType::Vector )
      {
        continue;
      }

      if ( layerTypeName( layer ) == typeName )
      {
        return qobject_cast<QgsVectorLayer *>( layer );
      }
    }
    return nullptr;
  }

  QgsFeatureRequest parseFilterElement( const QString &typeName, QDomElement &filterElem, QgsProject *project )
  {
    // Get the server feature ids in filter element
    QStringList collectedServerFids;
    return parseFilterElement( typeName, filterElem, collectedServerFids, project );
  }

  QgsFeatureRequest parseFilterElement( const QString &typeName, QDomElement &filterElem, QStringList &serverFids, const QgsProject *project, const QgsMapLayer *layer )
  {
    QgsFeatureRequest request;

    QDomNodeList fidNodes = filterElem.elementsByTagName( QStringLiteral( "FeatureId" ) );
    QDomNodeList goidNodes = filterElem.elementsByTagName( QStringLiteral( "GmlObjectId" ) );
    if ( !fidNodes.isEmpty() )
    {
      // Get the server feature ids in filter element
      QStringList collectedServerFids;
      QDomElement fidElem;
      for ( int f = 0; f < fidNodes.size(); f++ )
      {
        fidElem = fidNodes.at( f ).toElement();
        if ( !fidElem.hasAttribute( QStringLiteral( "fid" ) ) )
        {
          throw QgsRequestNotWellFormedException( "FeatureId element without fid attribute" );
        }

        QString serverFid = fidElem.attribute( QStringLiteral( "fid" ) );
        if ( serverFid.contains( QLatin1String( "." ) ) )
        {
          if ( serverFid.section( QStringLiteral( "." ), 0, 0 ) != typeName )
            continue;
          serverFid = serverFid.section( QStringLiteral( "." ), 1, 1 );
        }
        collectedServerFids << serverFid;
      }
      // No server feature ids found
      if ( collectedServerFids.isEmpty() )
      {
        throw QgsRequestNotWellFormedException( QStringLiteral( "No FeatureId element correctly parse against typeName '%1'" ).arg( typeName ) );
      }
      // update server feature ids
      serverFids.append( collectedServerFids );
      request.setFlags( Qgis::FeatureRequestFlag::NoFlags );
      return request;
    }
    else if ( !goidNodes.isEmpty() )
    {
      // Get the server feature ids in filter element
      QStringList collectedServerFids;
      QDomElement goidElem;
      for ( int f = 0; f < goidNodes.size(); f++ )
      {
        goidElem = goidNodes.at( f ).toElement();
        if ( !goidElem.hasAttribute( QStringLiteral( "id" ) ) && !goidElem.hasAttribute( QStringLiteral( "gml:id" ) ) )
        {
          throw QgsRequestNotWellFormedException( "GmlObjectId element without gml:id attribute" );
        }

        QString serverFid = goidElem.attribute( QStringLiteral( "id" ) );
        if ( serverFid.isEmpty() )
          serverFid = goidElem.attribute( QStringLiteral( "gml:id" ) );
        if ( serverFid.contains( QLatin1String( "." ) ) )
        {
          if ( serverFid.section( QStringLiteral( "." ), 0, 0 ) != typeName )
            continue;
          serverFid = serverFid.section( QStringLiteral( "." ), 1, 1 );
        }
        collectedServerFids << serverFid;
      }
      // No server feature ids found
      if ( collectedServerFids.isEmpty() )
      {
        throw QgsRequestNotWellFormedException( QStringLiteral( "No GmlObjectId element correctly parse against typeName '%1'" ).arg( typeName ) );
      }
      // update server feature ids
      serverFids.append( collectedServerFids );
      request.setFlags( Qgis::FeatureRequestFlag::NoFlags );
      return request;
    }
    else if ( filterElem.firstChildElement().tagName() == QLatin1String( "BBOX" ) )
    {
      QDomElement bboxElem = filterElem.firstChildElement();
      QDomElement childElem = bboxElem.firstChildElement();

      while ( !childElem.isNull() )
      {
        if ( childElem.tagName() == QLatin1String( "Box" ) )
        {
          request.setFilterRect( QgsOgcUtils::rectangleFromGMLBox( childElem ) );
        }
        else if ( childElem.tagName() != QLatin1String( "PropertyName" ) )
        {
          QgsOgcUtils::Context ctx { layer, project ? project->transformContext() : QgsCoordinateTransformContext() };
          QgsGeometry geom = QgsOgcUtils::geometryFromGML( childElem, ctx );
          request.setFilterRect( geom.boundingBox() );
        }
        childElem = childElem.nextSiblingElement();
      }

      request.setFlags( Qgis::FeatureRequestFlag::ExactIntersect | Qgis::FeatureRequestFlag::NoFlags );
      return request;
    }
    // Apply BBOX through filterRect even inside an And to use spatial index
    else if ( filterElem.firstChildElement().tagName() == QLatin1String( "And" ) && !filterElem.firstChildElement().firstChildElement( QLatin1String( "BBOX" ) ).isNull() )
    {
      int nbChildElem = filterElem.firstChildElement().childNodes().size();

      // Create a filter element to parse And child not BBOX
      QDomElement childFilterElement = filterElem.ownerDocument().createElement( QLatin1String( "Filter" ) );
      if ( nbChildElem > 2 )
      {
        QDomElement childAndElement = filterElem.ownerDocument().createElement( QLatin1String( "And" ) );
        childFilterElement.appendChild( childAndElement );
      }

      // Create a filter element to parse  BBOX
      QDomElement bboxFilterElement = filterElem.ownerDocument().createElement( QLatin1String( "Filter" ) );

      QDomElement childElem = filterElem.firstChildElement().firstChildElement();
      while ( !childElem.isNull() )
      {
        // Update request based on BBOX
        if ( childElem.tagName() == QLatin1String( "BBOX" ) )
        {
          // Clone BBOX
          bboxFilterElement.appendChild( childElem.cloneNode( true ) );
        }
        else
        {
          // Clone And child
          if ( nbChildElem > 2 )
          {
            childFilterElement.firstChildElement().appendChild( childElem.cloneNode( true ) );
          }
          else
          {
            childFilterElement.appendChild( childElem.cloneNode( true ) );
          }
        }
        childElem = childElem.nextSiblingElement();
      }

      // Parse the filter element with the cloned BBOX
      QStringList collectedServerFids;
      QgsFeatureRequest bboxRequest = parseFilterElement( typeName, bboxFilterElement, collectedServerFids, project );

      // Update request based on BBOX
      if ( request.filterRect().isEmpty() )
      {
        request.setFilterRect( bboxRequest.filterRect() );
      }
      else
      {
        request.setFilterRect( request.filterRect().intersect( bboxRequest.filterRect() ) );
      }

      // Parse the filter element with the cloned And child
      QgsFeatureRequest childRequest = parseFilterElement( typeName, childFilterElement, collectedServerFids, project );

      // Update server feature ids
      if ( !collectedServerFids.isEmpty() )
      {
        serverFids.append( collectedServerFids );
      }

      // Update expression
      request.setFilterExpression( childRequest.filterExpression()->expression() );

      request.setFlags( Qgis::FeatureRequestFlag::ExactIntersect | Qgis::FeatureRequestFlag::NoFlags );
      return request;
    }
    else
    {
      QgsVectorLayer *layer = nullptr;
      if ( project )
      {
        layer = layerByTypeName( project, typeName );
      }
      std::shared_ptr<QgsExpression> filter( QgsOgcUtils::expressionFromOgcFilter( filterElem, layer ) );
      if ( filter )
      {
        if ( filter->hasParserError() || !filter->parserErrorString().isEmpty() )
        {
          throw QgsRequestNotWellFormedException( filter->parserErrorString() );
        }

        if ( filter->needsGeometry() )
        {
          request.setFlags( Qgis::FeatureRequestFlag::NoFlags );
        }
        request.setFilterExpression( filter->expression() );
        return request;
      }
    }
    return request;
  }

  QByteArray domDocumentToByteArray( const QDomDocument &doc )
  {
#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
    // A document with a processing instruction or a document type is written in another way
    // when the encoding comes from the stream: these are left to Qt
    bool simple = doc.doctype().name().isEmpty();
    for ( QDomNode node = doc.firstChild(); simple && !node.isNull(); node = node.nextSibling() )
      simple = !node.isProcessingInstruction();

    if ( simple )
    {
      // the codecs are owned by Qt
      static QTextCodec *sCodec = new Utf8CheckCodec();
      // written first by QDomNode::save() when the encoding comes from the stream
      static const QString sDeclaration = QStringLiteral( "<?xml version=\"1.0\" encoding=\"%1\"?>\n" ).arg( QString::fromLatin1( sCodec->name() ) );

      QString xml;
      {
        QTextStream stream( &xml, QIODevice::WriteOnly );
        stream.setCodec( sCodec );
        doc.save( stream, 1, QDomNode::EncodingFromTextStream );
      }
      if ( xml.startsWith( sDeclaration ) )
        return xml.midRef( sDeclaration.size() ).toUtf8();
    }
#endif
    return doc.toByteArray();
  }

} // namespace QgsWfs

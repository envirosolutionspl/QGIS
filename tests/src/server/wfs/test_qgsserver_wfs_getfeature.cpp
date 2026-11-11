/***************************************************************************
     test_qgsserver_wfs_getfeature.cpp
     ---------------------------------
    Date                 : 12 Jan 2023
    Copyright            : (C) 2022 by Paul Blottiere
    Email                : paul dot blottiere @ gmail.com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgstest.h"
#include "qgsproject.h"
#include "qgsvectorlayer.h"
#include "qgsfeaturerequest.h"

#include "qgsbufferserverresponse.h"
#include "qgsserverinterfaceimpl.h"
#include "qgswfsgetfeature.h"
#include "qgswfsutils.h"

#include <QDomDocument>

/**
 * \ingroup UnitTests
 * This is a unit test for the WMS parameters class
 */
class TestQgsServerWfsGetFeature : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();

    void nullValueProperty();
    void domDocumentToByteArray();
};

void TestQgsServerWfsGetFeature::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();
}

void TestQgsServerWfsGetFeature::cleanupTestCase()
{
  QgsApplication::exitQgis();
}

void TestQgsServerWfsGetFeature::nullValueProperty()
{
  // init server iface
  QgsServiceRegistry registry;
  QgsServerSettings settings;
  QgsCapabilitiesCache cache( settings.capabilitiesCacheSize() );
  QgsServerInterfaceImpl interface( &cache, &registry, &settings );

  // init project
  QgsProject project;

  QgsVectorLayer *vl = new QgsVectorLayer( QStringLiteral( "Point?crs=epsg:4326&field=name0:int&field=name1:int&field=name2:int" ), QStringLiteral( "vl" ), QStringLiteral( "memory" ) );

  QgsFields fields;
  const QgsField field0( QStringLiteral( "name0" ) );
  fields.append( field0 );
  const QgsField field1( QStringLiteral( "name1" ) );
  fields.append( field1 );
  const QgsField field2( QStringLiteral( "name2" ) );
  fields.append( field2 );

  QgsFeature f;
  f.setFields( fields, true );
  f.setAttribute( 0, QVariant( 0 ) );
  f.setAttribute( 1, QVariant() );
  f.setAttribute( 2, QVariant() );
  f.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 0.50, 0.50 ) ) );
  std::cout << "feature is valid: " << f.isValid() << std::endl;

  vl->startEditing();
  vl->addFeature( f );
  vl->commitChanges();
  std::cout << "feature count: " << vl->featureCount() << std::endl;

  project.addMapLayer( vl );

  QStringList wfsLayers;
  wfsLayers << vl->id();
  project.writeEntry( "WFSLayers", "/", wfsLayers );

  //
  // GML2
  //

  // build response
  QgsServerRequest gml2Request;
  gml2Request.setParameter( "VERSION", "1.0.0" );
  gml2Request.setParameter( "TYPENAME", "vl" );
  gml2Request.setParameter( "OUTPUTFORMAT", "gml2" );

  QgsBufferServerResponse gml2Response;
  QgsWfs::writeGetFeature( &interface, &project, "", gml2Request, gml2Response );

  // check response
  QDomDocument gml2Xml;
  gml2Xml.setContent( gml2Response.body() );

  const QDomElement gml2Elem = gml2Xml.documentElement();
  const QDomNode gml2FeatureNode = gml2Elem.elementsByTagName( "gml:featureMember" ).at( 0 ).firstChild();
  const QDomNodeList gml2Children = gml2FeatureNode.childNodes();

  // attributes for fields name1 and name2 are NULL (QVariant()) for the
  // feature f, so it's not added in the resulting document. This way, the XML
  // only contains 3 children:
  //   - <gml:boundedBy>
  //   - <qgs:geometry>
  //   - <qgs:name0>0</qgs:name0>
  QCOMPARE( gml2Children.count(), 3 );

  //
  // GML3
  //

  // build response
  QgsServerRequest gml3Request;
  gml3Request.setParameter( "VERSION", "1.0.0" );
  gml3Request.setParameter( "TYPENAME", "vl" );
  gml3Request.setParameter( "OUTPUTFORMAT", "gml3" );

  QgsBufferServerResponse gml3Response;
  QgsWfs::writeGetFeature( &interface, &project, "", gml3Request, gml3Response );

  // check response
  QDomDocument gml3Xml;
  gml3Xml.setContent( gml3Response.body() );

  const QDomElement gml3Elem = gml3Xml.documentElement();
  const QDomNode gml3FeatureNode = gml3Elem.elementsByTagName( "gml:featureMember" ).at( 0 ).firstChild();
  const QDomNodeList gml3Children = gml3FeatureNode.childNodes();

  // attributes for fields name1 and name2 are NULL (QVariant()) for the
  // feature f, so it's not added in the resulting document. This way, the XML
  // only contains 3 children:
  //   - <gml:boundedBy>
  //   - <qgs:geometry>
  //   - <qgs:name0>0</qgs:name0>
  QCOMPARE( gml3Children.count(), 3 );
}

void TestQgsServerWfsGetFeature::domDocumentToByteArray()
{
  // elements with and without namespace, attributes, texts with the characters which are escaped,
  // non ASCII characters, a surrogate pair, a lone surrogate and non characters
  QDomDocument doc;
  QDomElement member = doc.createElement( QStringLiteral( "gml:featureMember" ) );
  doc.appendChild( member );
  QDomElement feature = doc.createElement( QStringLiteral( "qgs:layer" ) );
  feature.setAttribute( QStringLiteral( "gml:id" ), QStringLiteral( "layer.1" ) );
  feature.setAttribute( QStringLiteral( "note" ), QStringLiteral( "a \"quoted\" <value> & a tab\t, a line\nbreak, \u017c\u00f3\u0142w" ) );
  member.appendChild( feature );
  QDomElement geometry = doc.createElementNS( QStringLiteral( "http://www.opengis.net/gml" ), QStringLiteral( "gml:Point" ) );
  geometry.setAttribute( QStringLiteral( "srsName" ), QStringLiteral( "EPSG:4326" ) );
  QDomElement pos = doc.createElementNS( QStringLiteral( "http://www.opengis.net/gml" ), QStringLiteral( "gml:pos" ) );
  pos.setAttribute( QStringLiteral( "srsDimension" ), QStringLiteral( "2" ) );
  pos.appendChild( doc.createTextNode( QStringLiteral( "18.5 50.25" ) ) );
  geometry.appendChild( pos );
  feature.appendChild( geometry );
  feature.appendChild( doc.createElement( QStringLiteral( "qgs:empty" ) ) );

  const QStringList texts {
    QStringLiteral( "plain text" ),
    QStringLiteral( "<tag> & \"quotes\" and apostrophes ' ]]> end" ),
    QStringLiteral( "carriage\rreturn, line\nfeed, tab\tcharacter" ),
    QStringLiteral( "Za\u017c\u00f3\u0142\u0107 g\u0119\u015bl\u0105 ja\u017a\u0144, \u6771\u4eac, \u041c\u043e\u0441\u043a\u0432\u0430" ),
    QString::fromUtf8( "surrogate pair \xF0\x9F\x97\xBA end" ),
    QStringLiteral( "lone surrogate " ) + QChar( 0xD83D ) + QStringLiteral( " end" ),
    QStringLiteral( "non characters " ) + QChar( 0xFFFE ) + QChar( 0xFDD0 ) + QChar( 0xFFFF ) + QStringLiteral( " end" ),
    QString(),
  };
  int index = 0;
  for ( const QString &text : texts )
  {
    QDomElement field = doc.createElement( QStringLiteral( "qgs:field%1" ).arg( index++ ) );
    field.appendChild( doc.createTextNode( text ) );
    feature.appendChild( field );
  }

  const QByteArray expected = doc.toByteArray();
  QVERIFY( expected.contains( "&lt;tag>" ) );
  QCOMPARE( QgsWfs::domDocumentToByteArray( doc ), expected );

  // documents with a processing instruction or a document type
  QDomDocument withDeclaration;
  withDeclaration.appendChild( withDeclaration.createProcessingInstruction( QStringLiteral( "xml" ), QStringLiteral( "version=\"1.0\" encoding=\"ISO-8859-1\"" ) ) );
  QDomElement root = withDeclaration.createElement( QStringLiteral( "root" ) );
  root.appendChild( withDeclaration.createTextNode( QStringLiteral( "\u017c\u00f3\u0142w \u00e9" ) ) );
  withDeclaration.appendChild( root );
  QCOMPARE( QgsWfs::domDocumentToByteArray( withDeclaration ), withDeclaration.toByteArray() );

  QDomDocument withDoctype( QStringLiteral( "html" ) );
  withDoctype.appendChild( withDoctype.createElement( QStringLiteral( "html" ) ) );
  QCOMPARE( QgsWfs::domDocumentToByteArray( withDoctype ), withDoctype.toByteArray() );

  QCOMPARE( QgsWfs::domDocumentToByteArray( QDomDocument() ), QDomDocument().toByteArray() );
}

QGSTEST_MAIN( TestQgsServerWfsGetFeature )
#include "test_qgsserver_wfs_getfeature.moc"

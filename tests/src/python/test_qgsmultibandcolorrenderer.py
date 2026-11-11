"""QGIS Unit tests for QgsMultiBandColorRenderer.

From build dir, run:
ctest -R PyQgsMultiBandColorRenderer -V

.. note:: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

__author__ = "Michal Wloga"
__date__ = "11/11/2026"
__copyright__ = "Copyright 2026, The QGIS Project"

import os

import numpy
from osgeo import gdal

from qgis.core import QgsMultiBandColorRenderer, QgsRasterLayer, QgsRectangle
from qgis.PyQt.QtCore import QTemporaryDir
from qgis.PyQt.QtGui import qRgb, qRgba
import unittest
from qgis.testing import start_app, QgisTestCase

start_app()

RED = [[10, 0, 30, 40], [50, 60, 70, 80]]
GREEN = [[11, 21, 0, 41], [51, 61, 71, 81]]
BLUE = [[12, 22, 32, 0], [52, 62, 72, 82]]
# no data cells are transparent when the renderer has no color for them
NO_DATA_COLOR = qRgba(0, 0, 0, 0)


class TestQgsMultiBandColorRenderer(QgisTestCase):

    def setUp(self):
        self.temp_dir = QTemporaryDir()

    def create_layer(self, name, no_data_value=None):
        """Returns a layer of 4 x 2 cells of 1 unit, with three 8-bit bands"""
        path = os.path.join(self.temp_dir.path(), name)
        dataset = gdal.GetDriverByName("GTiff").Create(path, 4, 2, 3, gdal.GDT_Byte)
        dataset.SetGeoTransform([0, 1, 0, 2, 0, -1])
        for band, values in enumerate((RED, GREEN, BLUE), start=1):
            dataset.GetRasterBand(band).WriteArray(numpy.array(values, dtype=numpy.uint8))
            if no_data_value is not None:
                dataset.GetRasterBand(band).SetNoDataValue(no_data_value)
        dataset = None
        layer = QgsRasterLayer(path, name)
        self.assertTrue(layer.isValid())
        return layer

    def test_byte_bands_with_no_data_value(self):
        layer = self.create_layer("no_data_value.tif", 0)
        renderer = QgsMultiBandColorRenderer(layer.dataProvider(), 1, 2, 3)
        block = renderer.block(1, layer.extent(), 4, 2)

        for row in range(2):
            for column in range(4):
                values = RED[row][column], GREEN[row][column], BLUE[row][column]
                # a cell is no data as soon as one of its bands is
                expected = NO_DATA_COLOR if 0 in values else qRgb(*values)
                self.assertEqual(block.color(row, column), expected, (row, column))

    def test_byte_bands_without_no_data_value(self):
        layer = self.create_layer("no_no_data_value.tif")
        renderer = QgsMultiBandColorRenderer(layer.dataProvider(), 1, 2, 3)

        # 0 is a value like any other
        block = renderer.block(1, layer.extent(), 4, 2)
        for row in range(2):
            for column in range(4):
                expected = qRgb(RED[row][column], GREEN[row][column], BLUE[row][column])
                self.assertEqual(block.color(row, column), expected, (row, column))

        # one cell more than the raster on every side: these cells are no data
        block = renderer.block(1, QgsRectangle(-1, -1, 5, 3), 6, 4)
        for row in range(4):
            for column in range(6):
                if 1 <= row <= 2 and 1 <= column <= 4:
                    expected = qRgb(
                        RED[row - 1][column - 1],
                        GREEN[row - 1][column - 1],
                        BLUE[row - 1][column - 1],
                    )
                else:
                    expected = NO_DATA_COLOR
                self.assertEqual(block.color(row, column), expected, (row, column))


if __name__ == "__main__":
    unittest.main()

/*
   DrawPile - a collaborative drawing program.

   Copyright (C) 2009-2014 Calle Laakkonen

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/
#include <QDomDocument>
#include <QBuffer>
#include <QDebug>

#include "ora/zipwriter.h"
#include "ora/orawriter.h"
#include "core/annotation.h"
#include "core/layerstack.h"
#include "core/layer.h"
#include "core/rasterop.h" // for blending modes

namespace {

bool putPngInZip(ZipWriter &zip, const QString &filename, const QImage &image)
{
	QBuffer buf;
	image.save(&buf, "PNG");

	// PNG is already compressed, so no use attempting to recompress.
	zip.setCompressionPolicy(ZipWriter::NeverCompress);
	zip.addFile(filename, buf.data());

	return true;
}

bool writeStackXml(ZipWriter &zf, const paintcore::LayerStack *image)
{
	QDomDocument doc;
	QDomElement root = doc.createElement("image");
	doc.appendChild(root);
	// Width and height are required attributes
	root.setAttribute("w", image->width());
	root.setAttribute("h", image->height());

	QDomElement stack = doc.createElement("stack");
	root.appendChild(stack);

	// Add annotations
	// This will probably be replaced with proper text element support
	// once standardized.
	if(image->hasAnnotations()) {
		QDomElement annotationEls = doc.createElementNS("http://drawpile.sourceforge.net/", "annotations");
		annotationEls.setPrefix("drawpile");
		foreach(const paintcore::Annotation *a, image->annotations()) {
			QDomElement an = doc.createElementNS("http://drawpile.sourceforge.net/","a");
			an.setPrefix("drawpile");

			QRect ag = a->rect();
			an.setAttribute("x", ag.x());
			an.setAttribute("y", ag.y());
			an.setAttribute("w", ag.width());
			an.setAttribute("h", ag.height());
			an.setAttribute("bg", QString("#%1").arg(uint(a->backgroundColor().rgba()), 8, 16, QChar('0')));
			an.appendChild(doc.createCDATASection(a->text()));
			annotationEls.appendChild(an);
		}
		stack.appendChild(annotationEls);
	}

	// Add layers (topmost layer goes first in ORA)
	for(int i=image->layers()-1;i>=0;--i) {
		const paintcore::Layer *l = image->getLayerByIndex(i);

		QDomElement layer = doc.createElement("layer");
		layer.setAttribute("src", QString("data/layer%1.png").arg(i));
		layer.setAttribute("name", l->title());
		layer.setAttribute("opacity", QString::number(l->opacity() / 255.0, 'f', 3));
		if(l->hidden())
			layer.setAttribute("visibility", "hidden");
		if(l->blendmode() != 1)
			layer.setAttribute("composite-op", "svg:" + paintcore::svgBlendMode(l->blendmode()));

		stack.appendChild(layer);
	}

	zf.setCompressionPolicy(ZipWriter::AlwaysCompress);
	zf.addFile("stack.xml", doc.toByteArray());
	return true;
}

bool writeLayer(ZipWriter &zf, const paintcore::LayerStack *layers, int index)
{
	const paintcore::Layer *l = layers->getLayerByIndex(index);
	Q_ASSERT(l);

	return putPngInZip(zf, QString("data/layer%1.png").arg(index), l->toImage());
}

bool writeThumbnail(ZipWriter &zf, const paintcore::LayerStack *layers)
{
	QImage img = layers->toFlatImage(false);
	if(img.width() > 256 || img.height() > 256)
		img = img.scaled(QSize(256, 256), Qt::KeepAspectRatio, Qt::SmoothTransformation);

	return putPngInZip(zf, "Thumbnails/thumbnail.png", img);
}

}

namespace openraster {

bool saveOpenRaster(const QString& filename, const paintcore::LayerStack *image)
{
	QFile orafile(filename);
	if(!orafile.open(QIODevice::WriteOnly))
		return false;

	ZipWriter zf(&orafile);

	// The first entry of an OpenRaster file must be a
	// (uncompressed) file named "mimetype".
	zf.setCompressionPolicy(ZipWriter::NeverCompress);
	zf.addFile("mimetype", QByteArray("image/openraster"));

	// The stack XML contains the image structure
	// definition.
	writeStackXml(zf, image);

	// Each layer is written as an individual PNG image
	for(int i=image->layers()-1;i>=0;--i)
		writeLayer(zf, image, i);

	// A ready to use thumbnail for file managers etc.
	writeThumbnail(zf, image);

	zf.close();
	return true;
}

}

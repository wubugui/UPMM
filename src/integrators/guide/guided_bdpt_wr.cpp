/*
    This file is part of Mitsuba, a physically based rendering system.

    Copyright (c) 2007-2012 by Wenzel Jakob and others.

    Mitsuba is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License Version 3
    as published by the Free Software Foundation.

    Mitsuba is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <mitsuba/core/bitmap.h>
#include <mitsuba/core/fstream.h>
#include "guided_bdpt_wr.h"

MTS_NAMESPACE_BEGIN

/* ==================================================================== */
/*                             Work result                              */
/* ==================================================================== */

GuidedBDPTWorkResult::GuidedBDPTWorkResult(const GuidedBDPTConfiguration &conf,
		const ReconstructionFilter *rfilter, Vector2i blockSize) {
	/* Stores the 'camera image' -- this can be blocked when
	   spreading out work to multiple workers */
	if (blockSize == Vector2i(-1, -1))
		blockSize = Vector2i(conf.blockSize, conf.blockSize);

	m_block = new ImageBlock(Bitmap::ESpectrumAlphaWeight, blockSize, rfilter);
	m_block->setOffset(Point2i(0, 0));
	m_block->setSize(blockSize);

	if (conf.lightImage) {
		/* Stores the 'light image' -- every worker requires a
		   full-resolution version, since contributions of s==0
		   and s==1 paths can affect any pixel of this bitmap */
		m_lightImage = new ImageBlock(Bitmap::ESpectrum,
				conf.cropSize, rfilter);
		m_lightImage->setSize(conf.cropSize);
		m_lightImage->setOffset(Point2i(0, 0));
	}

	/* When debug mode is active, we additionally create
	   full-resolution bitmaps storing the contributions of
	   each individual sampling strategy */
#if GBDPT_DEBUG == 1
	m_debugBlocks.resize(
		conf.maxDepth*(5+conf.maxDepth)/2);
	m_debugBlocksM.resize(
		conf.maxDepth*(5 + conf.maxDepth) / 2);

	for (size_t i=0; i<m_debugBlocks.size(); ++i) {
		m_debugBlocks[i] = new ImageBlock(
				Bitmap::ESpectrum, conf.cropSize, rfilter);
		m_debugBlocks[i]->setOffset(Point2i(0,0));
		m_debugBlocks[i]->setSize(conf.cropSize);
	}
	for (size_t i = 0; i < m_debugBlocksM.size(); ++i) {
		m_debugBlocksM[i] = new ImageBlock(
			Bitmap::ESpectrum, conf.cropSize, rfilter);
		m_debugBlocksM[i]->setOffset(Point2i(0, 0));
		m_debugBlocksM[i]->setSize(conf.cropSize);
	}
#endif
}

GuidedBDPTWorkResult::~GuidedBDPTWorkResult() { }

void GuidedBDPTWorkResult::put(const GuidedBDPTWorkResult *workResult) {
#if GBDPT_DEBUG == 1
	for (size_t i=0; i<m_debugBlocks.size(); ++i)
		m_debugBlocks[i]->put(workResult->m_debugBlocks[i].get());
	for (size_t i = 0; i < m_debugBlocksM.size(); ++i)
		m_debugBlocksM[i]->put(workResult->m_debugBlocksM[i].get());
#endif
	m_block->put(workResult->m_block.get());
	if (m_lightImage)
		m_lightImage->put(workResult->m_lightImage.get());
}

void GuidedBDPTWorkResult::clear() {
#if GBDPT_DEBUG == 1
	for (size_t i=0; i<m_debugBlocks.size(); ++i)
		m_debugBlocks[i]->clear();
	for (size_t i = 0; i < m_debugBlocksM.size(); ++i)
		m_debugBlocksM[i]->clear();
#endif
	if (m_lightImage)
		m_lightImage->clear();
	m_block->clear();
}

#if GBDPT_DEBUG == 1
/* In debug mode, this function allows to dump the contributions of
   the individual sampling strategies to a series of images */
void GuidedBDPTWorkResult::dump(const GuidedBDPTConfiguration &conf,
		const fs::path &prefix, const fs::path &stem) const {
	Float weight = (Float) 1.0f / (Float) conf.sampleCount;
// 	for (int k = 1; k<=conf.maxDepth; ++k) {
// 		for (int t=0; t<=k+1; ++t) {
// 			size_t s = k+1-t;
// 			Bitmap *bitmap = const_cast<Bitmap *>(m_debugBlocks[strategyIndex(s, t)]->getBitmap());
// 			ref<Bitmap> ldrBitmap = bitmap->convert(Bitmap::ERGB, Bitmap::EFloat, -1, weight);
// 			fs::path filename =
// 				prefix / fs::path(formatString("%s_gbdpt_k%02i_s%02i_t%02i.pfm", stem.filename().string().c_str(), k, s, t));
// 			ref<FileStream> targetFile = new FileStream(filename,
// 				FileStream::ETruncReadWrite);
// 			ldrBitmap->write(Bitmap::EPFM, targetFile, 1);
// 		}
// 	}
	Bitmap* kmap = new Bitmap(Bitmap::ESpectrum, Bitmap::EFloat, conf.cropSize, -1);
	for (int k = 1; k <= conf.maxDepth; ++k) {
		kmap->clear();
		for (int t = 0; t <= k + 1; ++t) {
			size_t s = k + 1 - t;
			Bitmap *bitmap = const_cast<Bitmap *>(m_debugBlocks[strategyIndex(s, t)]->getBitmap());
			if (bitmap->average().isZero()) continue;
			kmap->accumulate(bitmap);
			ref<Bitmap> ldrBitmap = bitmap->convert(Bitmap::ERGB, Bitmap::EFloat32, -1, weight);
			fs::path filename =
				prefix / fs::path(formatString("%s_gbdpt_k%02i_s%02i_t%02i.pfm", stem.filename().string().c_str(), k, s, t));
			ref<FileStream> targetFile = new FileStream(filename,
				FileStream::ETruncReadWrite);
			ldrBitmap->write(Bitmap::EPFM, targetFile, 1);
		}
		ref<Bitmap> ldrBitmap = kmap->convert(Bitmap::ERGB, Bitmap::EFloat32, -1, weight);
		fs::path filename =
			prefix / fs::path(formatString("%s_gbdpt_k%02i.pfm", stem.filename().string().c_str(), k));
		ref<FileStream> targetFile = new FileStream(filename, FileStream::ETruncReadWrite);
		ldrBitmap->write(Bitmap::EPFM, targetFile, 1);

		for (int t = 0; t <= k + 1; ++t) {
			size_t s = k + 1 - t;
			Bitmap *bitmap = const_cast<Bitmap *>(m_debugBlocksM[strategyIndex(s, t)]->getBitmap());
			if (bitmap->average().isZero()) continue;
			ref<Bitmap> ldrBitmap = bitmap->convert(Bitmap::ERGB, Bitmap::EFloat32, -1, weight);
			fs::path filename =
				prefix / fs::path(formatString("%s_gbdpt_nm_k%02i_s%02i_t%02i.pfm", stem.filename().string().c_str(), k, s, t));
			ref<FileStream> targetFile = new FileStream(filename,
				FileStream::ETruncReadWrite);
			ldrBitmap->write(Bitmap::EPFM, targetFile, 1);
		}
	}
}
#endif

void GuidedBDPTWorkResult::load(Stream *stream) {
#if GBDPT_DEBUG == 1
	for (size_t i=0; i<m_debugBlocks.size(); ++i)
		m_debugBlocks[i]->load(stream);
#endif
	if (m_lightImage)
		m_lightImage->load(stream);
	m_block->load(stream);
}

void GuidedBDPTWorkResult::save(Stream *stream) const {
#if GBDPT_DEBUG == 1
	for (size_t i=0; i<m_debugBlocks.size(); ++i)
		m_debugBlocks[i]->save(stream);
#endif
	if (m_lightImage.get())
		m_lightImage->save(stream);
	m_block->save(stream);
}

std::string GuidedBDPTWorkResult::toString() const {
	return m_block->toString();
}

MTS_IMPLEMENT_CLASS(GuidedBDPTWorkResult, false, WorkResult)
MTS_NAMESPACE_END

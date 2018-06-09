/*
 *  Copyright (c) 2014 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "kis_ls_stroke_filter.h"

#include <cstdlib>

#include <QBitArray>

#include <resources/KoPattern.h>

#include <resources/KoAbstractGradient.h>

#include "psd.h"

#include "kis_convolution_kernel.h"
#include "kis_convolution_painter.h"
#include "kis_gaussian_kernel.h"

#include "kis_pixel_selection.h"
#include "kis_fill_painter.h"
#include "kis_gradient_painter.h"
#include "kis_iterator_ng.h"
#include "kis_random_accessor_ng.h"

#include "kis_psd_layer_style.h"
#include "kis_layer_style_filter_environment.h"

#include "kis_ls_utils.h"
#include "kis_multiple_projection.h"

namespace {

int borderSize(psd_stroke_position position, int size)
{
    int border = 0;

    switch (position) {
    case psd_stroke_outside:
        border = 2 * size + 1;
        break;
    case psd_stroke_center:
        border = size + 1;
        break;
    case psd_stroke_inside:
        border = 1;
        break;
    }

    return border;
}

}


KisLsStrokeFilter::KisLsStrokeFilter()
    : KisLayerStyleFilter(KoID("lsstroke", i18n("Stroke (style)")))
{
}

void KisLsStrokeFilter::applyStroke(KisPaintDeviceSP srcDevice,
                                    KisMultipleProjection *dst,
                                    const QRect &applyRect,
                                    const psd_layer_effects_stroke *config,
                                    KisLayerStyleFilterEnvironment *env) const
{
    if (applyRect.isEmpty()) return;

    const QRect needRect = kisGrowRect(applyRect, borderSize(config->position(), config->size()));

    KisSelectionSP baseSelection = KisLsUtils::selectionFromAlphaChannel(srcDevice, needRect);
    KisPixelSelectionSP selection = baseSelection->pixelSelection();

    {
        KisPixelSelectionSP knockOutSelection = new KisPixelSelection(new KisSelectionEmptyBounds(0));
        knockOutSelection->makeCloneFromRough(selection, needRect);

        if (config->position() == psd_stroke_outside) {
            KisGaussianKernel::applyDilate(selection, needRect, 2 * config->size(), QBitArray(), 0);
        } else if (config->position() == psd_stroke_inside) {
            KisGaussianKernel::applyErodeU8(knockOutSelection, needRect, 2 * config->size(), QBitArray(), 0);
        } else if (config->position() == psd_stroke_center) {
            KisGaussianKernel::applyDilate(selection, needRect, config->size(), QBitArray(), 0);
            KisGaussianKernel::applyErodeU8(knockOutSelection, needRect, config->size(), QBitArray(), 0);
        }

        KisPainter gc(selection);
        gc.setCompositeOp(COMPOSITE_ERASE);
        gc.bitBlt(needRect.topLeft(), knockOutSelection, needRect);
        gc.end();
    }

    KisPaintDeviceSP fillDevice = new KisPaintDevice(srcDevice->colorSpace());
    KisLsUtils::fillOverlayDevice(fillDevice, applyRect, config, env);


    const QString compositeOp = config->blendMode();
    const quint8 opacityU8 = 255.0 / 100.0 * config->opacity();
    KisPaintDeviceSP dstDevice = dst->getProjection(KisMultipleProjection::defaultProjectionId(),
                                                    compositeOp,
                                                    opacityU8,
                                                    QBitArray(),
                                                    srcDevice);

    KisPainter::copyAreaOptimized(applyRect.topLeft(), fillDevice, dstDevice, applyRect, baseSelection);
}

void KisLsStrokeFilter::processDirectly(KisPaintDeviceSP src,
                                        KisMultipleProjection *dst,
                                         const QRect &applyRect,
                                         KisPSDLayerStyleSP style,
                                         KisLayerStyleFilterEnvironment *env) const
{
    Q_UNUSED(env);
    KIS_ASSERT_RECOVER_RETURN(style);

    const psd_layer_effects_stroke *config = style->stroke();
    if (!KisLsUtils::checkEffectEnabled(config, dst)) return;

    KisLsUtils::LodWrapper<psd_layer_effects_stroke> w(env->currentLevelOfDetail(), config);
    applyStroke(src, dst, applyRect, w.config, env);
}

QRect KisLsStrokeFilter::neededRect(const QRect &rect, KisPSDLayerStyleSP style, KisLayerStyleFilterEnvironment *env) const
{
    const psd_layer_effects_stroke *config = style->stroke();
    if (!config->effectEnabled()) return rect;

    KisLsUtils::LodWrapper<psd_layer_effects_stroke> w(env->currentLevelOfDetail(), config);
    return kisGrowRect(rect, borderSize(w.config->position(), w.config->size()));
}

QRect KisLsStrokeFilter::changedRect(const QRect &rect, KisPSDLayerStyleSP style, KisLayerStyleFilterEnvironment *env) const
{
    return neededRect(rect, style, env);
}

/*
 * Copyright (c) 2008 Cyrille Berger <cberger@cberger.net>
 * Copyright (c) 2010 Geoffry Song <goffrie@gmail.com>
 *
 *  This library is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <kis_assistant_tool.h>

#include <QPainter>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QStandardPaths>
#include <QFile>
#include <QLineF>

#include <kis_debug.h>
#include <klocalizedstring.h>
#include <KColorButton>
#include <QMessageBox>

#include <KoIcon.h>
#include <KoFileDialog.h>
#include <KoViewConverter.h>
#include <KoPointerEvent.h>
#include <KoSnapGuide.h>

#include <canvas/kis_canvas2.h>
#include <kis_canvas_resource_provider.h>
#include <kis_cursor.h>
#include <kis_image.h>
#include <KisViewManager.h>
#include <kis_icon.h>
#include <kis_abstract_perspective_grid.h>
#include <kis_painting_assistants_decoration.h>
#include "kis_global.h"


#include <math.h>

KisAssistantTool::KisAssistantTool(KoCanvasBase * canvas)
    : KisTool(canvas, KisCursor::arrowCursor()), m_canvas(dynamic_cast<KisCanvas2*>(canvas)),
      m_assistantDrag(0), m_newAssistant(0), m_optionsWidget(0)
{
    Q_ASSERT(m_canvas);
    setObjectName("tool_assistanttool");
}

KisAssistantTool::~KisAssistantTool()
{
}

void KisAssistantTool::activate(ToolActivation toolActivation, const QSet<KoShape*> &shapes)
{
    // Add code here to initialize your tool when it got activated
    KisTool::activate(toolActivation, shapes);

    m_canvas->paintingAssistantsDecoration()->activateAssistantsEditor();
    m_handles = m_canvas->paintingAssistantsDecoration()->handles();

    repaintDecorations();

    m_handleDrag = 0;
    m_internalMode = MODE_CREATION;
    m_assistantHelperYOffset = 10;

    m_iconDelete = KisIconUtils::loadIcon("dialog-cancel").pixmap(16, 16);
    m_iconSnapOn = KisIconUtils::loadIcon("visible").pixmap(16, 16);
    m_iconSnapOff = KisIconUtils::loadIcon("novisible").pixmap(16, 16);
    m_iconMove = KisIconUtils::loadIcon("transform-move").pixmap(32, 32);
    m_handleSize = 17;
}

void KisAssistantTool::deactivate()
{
    m_canvas->paintingAssistantsDecoration()->deactivateAssistantsEditor();
    repaintDecorations(); // helps with updating assistant decoration to a non-editor look
    KisTool::deactivate();
}

void KisAssistantTool::beginPrimaryAction(KoPointerEvent *event)
{
    setMode(KisTool::PAINT_MODE);
    bool newAssistantAllowed = true;

    KisPaintingAssistantsDecorationSP canvasDecoration = m_canvas->paintingAssistantsDecoration();

    if (m_newAssistant) {
        m_internalMode = MODE_CREATION;
        *m_newAssistant->handles().back() = canvasDecoration->snapToGuide(event, QPointF(), false);
        if (m_newAssistant->handles().size() == m_newAssistant->numHandles()) {
            addAssistant();
        } else {
            m_newAssistant->addHandle(new KisPaintingAssistantHandle(canvasDecoration->snapToGuide(event, QPointF(), false)), HandleType::NORMAL);
        }
        m_canvas->updateCanvas();
        return;
    }
    m_handleDrag = 0;
    double minDist = 81.0;


    QPointF mousePos = m_canvas->viewConverter()->documentToView(canvasDecoration->snapToGuide(event, QPointF(), false));//m_canvas->viewConverter()->documentToView(event->point);


    Q_FOREACH (KisPaintingAssistantSP assistant, m_canvas->paintingAssistantsDecoration()->assistants()) {

        // find out which handle on all assistants is closes to the mouse position
        Q_FOREACH (const KisPaintingAssistantHandleSP handle, m_handles) {
            double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*handle));
            if (dist < minDist) {
                minDist = dist;
                m_handleDrag = handle;
            }
        }


        if(m_handleDrag && assistant->id() == "perspective") {
            // Look for the handle which was pressed


            if (m_handleDrag == assistant->topLeft()) {
                double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*m_handleDrag));
                if (dist < minDist) {
                    minDist = dist;
                }
                m_dragStart = QPointF(assistant->topRight().data()->x(),assistant->topRight().data()->y());
                m_internalMode = MODE_DRAGGING_NODE;
            } else if (m_handleDrag == assistant->topRight()) {
                double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*m_handleDrag));
                if (dist < minDist) {
                    minDist = dist;
                }
                m_internalMode = MODE_DRAGGING_NODE;
                m_dragStart = QPointF(assistant->topLeft().data()->x(),assistant->topLeft().data()->y());
            } else if (m_handleDrag == assistant->bottomLeft()) {
                double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*m_handleDrag));
                if (dist < minDist) {
                    minDist = dist;
                }
                m_internalMode = MODE_DRAGGING_NODE;
                m_dragStart = QPointF(assistant->bottomRight().data()->x(),assistant->bottomRight().data()->y());
            } else if (m_handleDrag == assistant->bottomRight()) {
                double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*m_handleDrag));
                if (dist < minDist) {
                    minDist = dist;
                }
                m_internalMode = MODE_DRAGGING_NODE;
                m_dragStart = QPointF(assistant->bottomLeft().data()->x(),assistant->bottomLeft().data()->y());
            } else if (m_handleDrag == assistant->leftMiddle()) {
                m_internalMode = MODE_DRAGGING_TRANSLATING_TWONODES;
                m_dragStart = QPointF((assistant->bottomLeft().data()->x()+assistant->topLeft().data()->x())*0.5,
                                      (assistant->bottomLeft().data()->y()+assistant->topLeft().data()->y())*0.5);
                m_selectedNode1 = new KisPaintingAssistantHandle(assistant->topLeft().data()->x(),assistant->topLeft().data()->y());
                m_selectedNode2 = new KisPaintingAssistantHandle(assistant->bottomLeft().data()->x(),assistant->bottomLeft().data()->y());
                m_newAssistant = toQShared(KisPaintingAssistantFactoryRegistry::instance()->get("perspective")->createPaintingAssistant());
                m_newAssistant->addHandle(assistant->topLeft(), HandleType::NORMAL );
                m_newAssistant->addHandle(m_selectedNode1, HandleType::NORMAL);
                m_newAssistant->addHandle(m_selectedNode2, HandleType::NORMAL);
                m_newAssistant->addHandle(assistant->bottomLeft(), HandleType::NORMAL);
                m_dragEnd = event->point;
                m_handleDrag = 0;
                m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
                return;
            } else if (m_handleDrag == assistant->rightMiddle()) {
                m_dragStart = QPointF((assistant->topRight().data()->x()+assistant->bottomRight().data()->x())*0.5,
                                      (assistant->topRight().data()->y()+assistant->bottomRight().data()->y())*0.5);
                m_internalMode = MODE_DRAGGING_TRANSLATING_TWONODES;
                m_selectedNode1 = new KisPaintingAssistantHandle(assistant->topRight().data()->x(),assistant->topRight().data()->y());
                m_selectedNode2 = new KisPaintingAssistantHandle(assistant->bottomRight().data()->x(),assistant->bottomRight().data()->y());
                m_newAssistant = toQShared(KisPaintingAssistantFactoryRegistry::instance()->get("perspective")->createPaintingAssistant());
                m_newAssistant->addHandle(assistant->topRight(), HandleType::NORMAL);
                m_newAssistant->addHandle(m_selectedNode1, HandleType::NORMAL);
                m_newAssistant->addHandle(m_selectedNode2, HandleType::NORMAL);
                m_newAssistant->addHandle(assistant->bottomRight(), HandleType::NORMAL);
                m_dragEnd = event->point;
                m_handleDrag = 0;
                m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
                return;
            } else if (m_handleDrag == assistant->topMiddle()) {
                m_dragStart = QPointF((assistant->topLeft().data()->x()+assistant->topRight().data()->x())*0.5,
                                      (assistant->topLeft().data()->y()+assistant->topRight().data()->y())*0.5);
                m_internalMode = MODE_DRAGGING_TRANSLATING_TWONODES;
                m_selectedNode1 = new KisPaintingAssistantHandle(assistant->topLeft().data()->x(),assistant->topLeft().data()->y());
                m_selectedNode2 = new KisPaintingAssistantHandle(assistant->topRight().data()->x(),assistant->topRight().data()->y());
                m_newAssistant = toQShared(KisPaintingAssistantFactoryRegistry::instance()->get("perspective")->createPaintingAssistant());
                m_newAssistant->addHandle(m_selectedNode1, HandleType::NORMAL);
                m_newAssistant->addHandle(m_selectedNode2, HandleType::NORMAL);
                m_newAssistant->addHandle(assistant->topRight(), HandleType::NORMAL);
                m_newAssistant->addHandle(assistant->topLeft(), HandleType::NORMAL);
                m_dragEnd = event->point;
                m_handleDrag = 0;
                m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
                return;
            } else if (m_handleDrag == assistant->bottomMiddle()) {
                m_dragStart = QPointF((assistant->bottomLeft().data()->x()+assistant->bottomRight().data()->x())*0.5,
                                      (assistant->bottomLeft().data()->y()+assistant->bottomRight().data()->y())*0.5);
                m_internalMode = MODE_DRAGGING_TRANSLATING_TWONODES;
                m_selectedNode1 = new KisPaintingAssistantHandle(assistant->bottomLeft().data()->x(),assistant->bottomLeft().data()->y());
                m_selectedNode2 = new KisPaintingAssistantHandle(assistant->bottomRight().data()->x(),assistant->bottomRight().data()->y());
                m_newAssistant = toQShared(KisPaintingAssistantFactoryRegistry::instance()->get("perspective")->createPaintingAssistant());
                m_newAssistant->addHandle(assistant->bottomLeft(), HandleType::NORMAL);
                m_newAssistant->addHandle(assistant->bottomRight(), HandleType::NORMAL);
                m_newAssistant->addHandle(m_selectedNode2, HandleType::NORMAL);
                m_newAssistant->addHandle(m_selectedNode1, HandleType::NORMAL);
                m_dragEnd = event->point;
                m_handleDrag = 0;
                m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
                return;
            }
            m_snapIsRadial = false;
        }
        else if (m_handleDrag && assistant->handles().size()>1 && (assistant->id() == "ruler" ||
                                                                   assistant->id() == "parallel ruler" ||
                                                                   assistant->id() == "infinite ruler" ||
                                                                   assistant->id() == "spline")){
            if (m_handleDrag == assistant->handles()[0]) {
                m_dragStart = *assistant->handles()[1];
            } else if (m_handleDrag == assistant->handles()[1]) {
                m_dragStart = *assistant->handles()[0];
            } else if(assistant->handles().size()==4){
                if (m_handleDrag == assistant->handles()[2]) {
                    m_dragStart = *assistant->handles()[0];
                } else if (m_handleDrag == assistant->handles()[3]) {
                    m_dragStart = *assistant->handles()[1];
                }
            }
            m_snapIsRadial = false;
        } else if (m_handleDrag && assistant->handles().size()>2 && (assistant->id() == "ellipse" ||
                                    assistant->id() == "concentric ellipse" ||
                                    assistant->id() == "fisheye-point")){
            m_snapIsRadial = false;
            if (m_handleDrag == assistant->handles()[0]) {
                m_dragStart = *assistant->handles()[1];
            } else if (m_handleDrag == assistant->handles()[1]) {
                m_dragStart = *assistant->handles()[0];
            } else if (m_handleDrag == assistant->handles()[2]) {
                m_dragStart = assistant->buttonPosition();
                m_radius = QLineF(m_dragStart, *assistant->handles()[0]);
                m_snapIsRadial = true;
            }

        } else {
            m_dragStart = assistant->buttonPosition();
            m_snapIsRadial = false;
        }
    }

    if (m_handleDrag) {
        // TODO: Shift-press should now be handled using the alternate actions
        // if (event->modifiers() & Qt::ShiftModifier) {
        //     m_handleDrag->uncache();
        //     m_handleDrag = m_handleDrag->split()[0];
        //     m_handles = m_canvas->view()->paintingAssistantsDecoration()->handles();
        // }
        m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
        return;
    }

    m_assistantDrag.clear();
    Q_FOREACH (KisPaintingAssistantSP assistant, m_canvas->paintingAssistantsDecoration()->assistants()) {

        // This code contains the click event behavior. The actual display of the icons are done at the bottom
        // of the paint even. Make sure the rectangles positions are the same between the two.

        // TODO: These 6 lines are duplicated below in the paint layer. It shouldn't be done like this.
        QPointF actionsPosition = m_canvas->viewConverter()->documentToView(assistant->buttonPosition());

        QPointF iconDeletePosition(actionsPosition + QPointF(78, m_assistantHelperYOffset + 7));
        QPointF iconSnapPosition(actionsPosition + QPointF(54, m_assistantHelperYOffset + 7));
        QPointF iconMovePosition(actionsPosition + QPointF(15, m_assistantHelperYOffset));


        QRectF deleteRect(iconDeletePosition, QSizeF(16, 16));
        QRectF visibleRect(iconSnapPosition, QSizeF(16, 16));
        QRectF moveRect(iconMovePosition, QSizeF(32, 32));

        if (moveRect.contains(mousePos)) {
            m_assistantDrag = assistant;
            m_cursorStart = event->point;
            m_currentAdjustment = QPointF();
            m_internalMode = MODE_EDITING;
            return;
        }
        if (deleteRect.contains(mousePos)) {
            removeAssistant(assistant);
            if(m_canvas->paintingAssistantsDecoration()->assistants().isEmpty()) {
                m_internalMode = MODE_CREATION;
            }
            else
                m_internalMode = MODE_EDITING;
            m_canvas->updateCanvas();
            return;
        }
        if (visibleRect.contains(mousePos)) {
            newAssistantAllowed = false;
            assistant->setSnappingActive(!assistant->isSnappingActive()); // toggle
            assistant->uncache();//this updates the chache of the assistant, very important.
        }
    }
    if (newAssistantAllowed==true){//don't make a new assistant when I'm just toogling visiblity//
        QString key = m_options.availableAssistantsComboBox->model()->index( m_options.availableAssistantsComboBox->currentIndex(), 0 ).data(Qt::UserRole).toString();
        m_newAssistant = toQShared(KisPaintingAssistantFactoryRegistry::instance()->get(key)->createPaintingAssistant());
        m_internalMode = MODE_CREATION;
        m_newAssistant->addHandle(new KisPaintingAssistantHandle(canvasDecoration->snapToGuide(event, QPointF(), false)), HandleType::NORMAL);
        if (m_newAssistant->numHandles() <= 1) {
            if (key == "vanishing point"){
                m_newAssistant->addHandle(new KisPaintingAssistantHandle(event->point+QPointF(-70,0)), HandleType::SIDE);
                m_newAssistant->addHandle(new KisPaintingAssistantHandle(event->point+QPointF(-140,0)), HandleType::SIDE);
                m_newAssistant->addHandle(new KisPaintingAssistantHandle(event->point+QPointF(70,0)), HandleType::SIDE);
                m_newAssistant->addHandle(new KisPaintingAssistantHandle(event->point+QPointF(140,0)), HandleType::SIDE);
                }
            addAssistant();
        } else {
            m_newAssistant->addHandle(new KisPaintingAssistantHandle(canvasDecoration->snapToGuide(event, QPointF(), false)), HandleType::NORMAL);
        }
    }

    m_canvas->updateCanvas();
}

void KisAssistantTool::continuePrimaryAction(KoPointerEvent *event)
{
    KisPaintingAssistantsDecorationSP canvasDecoration = m_canvas->paintingAssistantsDecoration();

    if (m_handleDrag) {
        *m_handleDrag = event->point;
        //ported from the gradient tool... we need to think about this more in the future.
        if (event->modifiers() == Qt::ShiftModifier && m_snapIsRadial) {
            QLineF dragRadius = QLineF(m_dragStart, event->point);
            dragRadius.setLength(m_radius.length());
            *m_handleDrag = dragRadius.p2();
        } else if (event->modifiers() == Qt::ShiftModifier ) {
            QPointF move = snapToClosestAxis(event->point - m_dragStart);
            *m_handleDrag = m_dragStart + move;
        } else {
            *m_handleDrag = canvasDecoration->snapToGuide(event, QPointF(), false);
        }
        m_handleDrag->uncache();

        m_handleCombine = 0;
        if (!(event->modifiers() & Qt::ShiftModifier)) {
            double minDist = 49.0;
            QPointF mousePos = m_canvas->viewConverter()->documentToView(event->point);
            Q_FOREACH (const KisPaintingAssistantHandleSP handle, m_handles) {
                if (handle == m_handleDrag)
                    continue;


                double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*handle));
                if (dist < minDist) {
                    minDist = dist;
                    m_handleCombine = handle;
                }
            }
        }
        m_canvas->updateCanvas();
    } else if (m_assistantDrag) {
        QPointF newAdjustment = canvasDecoration->snapToGuide(event, QPointF(), false) - m_cursorStart;
        if (event->modifiers() == Qt::ShiftModifier ) {
            newAdjustment = snapToClosestAxis(newAdjustment);
        }
        Q_FOREACH (KisPaintingAssistantHandleSP handle, m_assistantDrag->handles()) {
            *handle += (newAdjustment - m_currentAdjustment);
        }
        if (m_assistantDrag->id()== "vanishing point"){
            Q_FOREACH (KisPaintingAssistantHandleSP handle, m_assistantDrag->sideHandles()) {
                *handle += (newAdjustment - m_currentAdjustment);
            }
        }
        m_currentAdjustment = newAdjustment;
        m_canvas->updateCanvas();

    } else {
        event->ignore();
    }

    bool wasHiglightedNode = m_higlightedNode != 0;
    QPointF mousep = m_canvas->viewConverter()->documentToView(event->point);
    QList <KisPaintingAssistantSP> pAssistant= m_canvas->paintingAssistantsDecoration()->assistants();

    Q_FOREACH (KisPaintingAssistantSP assistant, pAssistant) {
        if(assistant->id() == "perspective") {
            if ((m_higlightedNode = assistant->closestCornerHandleFromPoint(mousep))) {
                if (m_higlightedNode == m_selectedNode1 || m_higlightedNode == m_selectedNode2) {
                    m_higlightedNode = 0;
                } else {
                    m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
                    break;
                }
            }
        }

        //this following bit sets the translations for the vanishing-point handles.
        if(m_handleDrag && assistant->id() == "vanishing point" && assistant->sideHandles().size()==4) {
            //for inner handles, the outer handle gets translated.
            if (m_handleDrag == assistant->sideHandles()[0]) {
                QLineF perspectiveline = QLineF(*assistant->handles()[0],
                                                *assistant->sideHandles()[0]);

                qreal length = QLineF(*assistant->sideHandles()[0],
                                      *assistant->sideHandles()[1]).length();

                if (length < 2.0){
                    length = 2.0;
                }

                length += perspectiveline.length();
                perspectiveline.setLength(length);
                *assistant->sideHandles()[1] = perspectiveline.p2();
            }            
            else if (m_handleDrag == assistant->sideHandles()[2]){
                QLineF perspectiveline = QLineF(*assistant->handles()[0], *assistant->sideHandles()[2]);
                qreal length = QLineF(*assistant->sideHandles()[2], *assistant->sideHandles()[3]).length();

                if (length<2.0){
                    length=2.0;
                }

                length += perspectiveline.length();
                perspectiveline.setLength(length);
                *assistant->sideHandles()[3] = perspectiveline.p2();
            } // for outer handles, only the vanishing point is translated, but only if there's an intersection.
            else if (m_handleDrag == assistant->sideHandles()[1]|| m_handleDrag == assistant->sideHandles()[3]){
                QPointF vanishingpoint(0,0);
                QLineF perspectiveline = QLineF(*assistant->sideHandles()[0], *assistant->sideHandles()[1]);
                QLineF perspectiveline2 = QLineF(*assistant->sideHandles()[2], *assistant->sideHandles()[3]);

                if (QLineF(perspectiveline2).intersect(QLineF(perspectiveline), &vanishingpoint) != QLineF::NoIntersection){
                    *assistant->handles()[0] = vanishingpoint;
                }
            }// and for the vanishing point itself, only the outer handles get translated.
            else if (m_handleDrag == assistant->handles()[0]){
                QLineF perspectiveline = QLineF(*assistant->handles()[0], *assistant->sideHandles()[0]);
                QLineF perspectiveline2 = QLineF(*assistant->handles()[0], *assistant->sideHandles()[2]);
                qreal length =  QLineF(*assistant->sideHandles()[0], *assistant->sideHandles()[1]).length();
                qreal length2 = QLineF(*assistant->sideHandles()[2], *assistant->sideHandles()[3]).length();

                if (length < 2.0) {
                    length = 2.0;
                }

                if (length2 < 2.0) {
                    length2=2.0;
                }

                length += perspectiveline.length();
                length2 += perspectiveline2.length();
                perspectiveline.setLength(length);
                perspectiveline2.setLength(length2);
                *assistant->sideHandles()[1] = perspectiveline.p2();
                *assistant->sideHandles()[3] = perspectiveline2.p2();
            }

        }
    }
    if (wasHiglightedNode && !m_higlightedNode) {
        m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
    }
}

void KisAssistantTool::endPrimaryAction(KoPointerEvent *event)
{
    setMode(KisTool::HOVER_MODE);

    if (m_handleDrag) {
        if (!(event->modifiers() & Qt::ShiftModifier) && m_handleCombine) {
            m_handleCombine->mergeWith(m_handleDrag);
            m_handleCombine->uncache();
            m_handles = m_canvas->paintingAssistantsDecoration()->handles();
        }
        m_handleDrag = m_handleCombine = 0;
        m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
    } else if (m_assistantDrag) {
        m_assistantDrag.clear();
        m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
    } else if(m_internalMode == MODE_DRAGGING_TRANSLATING_TWONODES) {
        addAssistant();
        m_internalMode = MODE_CREATION;
        m_canvas->updateCanvas();
    }
    else {
        event->ignore();
    }
}

void KisAssistantTool::addAssistant()
{
    m_canvas->paintingAssistantsDecoration()->addAssistant(m_newAssistant);
    m_handles = m_canvas->paintingAssistantsDecoration()->handles();
    KisAbstractPerspectiveGrid* grid = dynamic_cast<KisAbstractPerspectiveGrid*>(m_newAssistant.data());
    if (grid) {
        m_canvas->viewManager()->resourceProvider()->addPerspectiveGrid(grid);
    }
    m_newAssistant.clear();
}

void KisAssistantTool::removeAssistant(KisPaintingAssistantSP assistant)
{
    KisAbstractPerspectiveGrid* grid = dynamic_cast<KisAbstractPerspectiveGrid*>(assistant.data());
    if (grid) {
        m_canvas->viewManager()->resourceProvider()->removePerspectiveGrid(grid);
    }
    m_canvas->paintingAssistantsDecoration()->removeAssistant(assistant);
    m_handles = m_canvas->paintingAssistantsDecoration()->handles();
}

void KisAssistantTool::mouseMoveEvent(KoPointerEvent *event)
{
    if (m_newAssistant && m_internalMode == MODE_CREATION) {
        *m_newAssistant->handles().back() = event->point;
        m_canvas->updateCanvas();
    } else if (m_newAssistant && m_internalMode == MODE_DRAGGING_TRANSLATING_TWONODES) {
        QPointF translate = event->point - m_dragEnd;;
        m_dragEnd = event->point;
        m_selectedNode1.data()->operator = (QPointF(m_selectedNode1.data()->x(),m_selectedNode1.data()->y()) + translate);
        m_selectedNode2.data()->operator = (QPointF(m_selectedNode2.data()->x(),m_selectedNode2.data()->y()) + translate);
        m_canvas->updateCanvas();
    }
}

void KisAssistantTool::paint(QPainter& _gc, const KoViewConverter &_converter)
{
    QRectF canvasSize = QRectF(QPointF(0, 0), QSizeF(m_canvas->image()->size()));
    QColor assistantColor = m_canvas->paintingAssistantsDecoration()->assistantsColor();
    QBrush fillAssistantColorBrush;
    fillAssistantColorBrush.setColor(m_canvas->paintingAssistantsDecoration()->assistantsColor());

    // show special display while a new assistant is in the process of being created
    if (m_newAssistant) {

        m_newAssistant->setAssistantColor(assistantColor);
        m_newAssistant->drawAssistant(_gc, canvasSize, m_canvas->coordinatesConverter(), false,m_canvas, true, false);

        Q_FOREACH (const KisPaintingAssistantHandleSP handle, m_newAssistant->handles()) {
            QPainterPath path;
            path.addEllipse(QRectF(_converter.documentToView(*handle) -  QPointF(m_handleSize * 0.5, m_handleSize * 0.5), QSizeF(m_handleSize, m_handleSize)));

            _gc.save();
            _gc.setPen(Qt::NoPen);
            _gc.setBrush(assistantColor);
            _gc.drawPath(path);
            _gc.restore();
        }
    }


    Q_FOREACH (KisPaintingAssistantSP assistant, m_canvas->paintingAssistantsDecoration()->assistants()) {

        // Draw corner and middle handles
        Q_FOREACH (const KisPaintingAssistantHandleSP handle, m_handles) {
            QRectF ellipse(_converter.documentToView(*handle) -  QPointF(m_handleSize * 0.5, m_handleSize * 0.5),
                           QSizeF(m_handleSize, m_handleSize));

            // render handles when they are being dragged and moved
            if (handle == m_handleDrag || handle == m_handleCombine) {
                _gc.save();
                _gc.setPen(Qt::NoPen);
                _gc.setBrush(assistantColor);
                _gc.drawEllipse(ellipse);
                _gc.restore();
            }

            QPainterPath path;

            path.addEllipse(ellipse);

            _gc.save();
            _gc.setPen(Qt::NoPen);
            _gc.setBrush(assistantColor);
            _gc.drawPath(path);
            _gc.restore();
        }

        // Draw middle perspective handles (this should probably be moved)
        if(assistant->id()=="perspective") {
            assistant->findPerspectiveAssistantHandleLocation();

            QPointF topMiddle, bottomMiddle, rightMiddle, leftMiddle;
            topMiddle = (_converter.documentToView(*assistant->topLeft()) + _converter.documentToView(*assistant->topRight()))*0.5;
            bottomMiddle = (_converter.documentToView(*assistant->bottomLeft()) + _converter.documentToView(*assistant->bottomRight()))*0.5;
            rightMiddle = (_converter.documentToView(*assistant->topRight()) + _converter.documentToView(*assistant->bottomRight()))*0.5;
            leftMiddle = (_converter.documentToView(*assistant->topLeft()) + _converter.documentToView(*assistant->bottomLeft()))*0.5;

            QPainterPath path;
            path.addEllipse(QRectF(leftMiddle - QPointF(m_handleSize * 0.5, m_handleSize * 0.5), QSizeF(m_handleSize, m_handleSize)));
            path.addEllipse(QRectF(topMiddle - QPointF(m_handleSize * 0.5, m_handleSize * 0.5), QSizeF(m_handleSize, m_handleSize)));
            path.addEllipse(QRectF(rightMiddle - QPointF(m_handleSize * 0.5, m_handleSize * 0.5), QSizeF(m_handleSize, m_handleSize)));
            path.addEllipse(QRectF(bottomMiddle - QPointF(m_handleSize * 0.5, m_handleSize * 0.5), QSizeF(m_handleSize, m_handleSize)));

            assistant->drawPath(_gc, path);
        }

        // draw editor specific controls for the assistant (this probably should be moved)
        if(assistant->id()=="vanishing point") {
            if (assistant->sideHandles().size() == 4) {
                // Draw the line
                QPointF p0 = _converter.documentToView(*assistant->handles()[0]);
                QPointF p1 = _converter.documentToView(*assistant->sideHandles()[0]);
                QPointF p2 = _converter.documentToView(*assistant->sideHandles()[1]);
                QPointF p3 = _converter.documentToView(*assistant->sideHandles()[2]);
                QPointF p4 = _converter.documentToView(*assistant->sideHandles()[3]);



                // Draw control lines
                // setting it here updates the vanishing point lines to correct color
                // this should probably move to vanishing point assistant class where everything else is done
                QPen penStyle(m_canvas->paintingAssistantsDecoration()->assistantsColor(), 2.0, Qt::SolidLine);
                _gc.save();
                _gc.setPen(penStyle);
                _gc.drawLine(p0, p1);
                _gc.drawLine(p0, p3);
                _gc.drawLine(p1, p2);
                _gc.drawLine(p3, p4);
                _gc.restore();
            }
        }       

        drawEditorWidget(assistant, _gc);
    }

}

void KisAssistantTool::removeAllAssistants()
{
    m_canvas->viewManager()->resourceProvider()->clearPerspectiveGrids();
    m_canvas->paintingAssistantsDecoration()->removeAll();
    m_handles = m_canvas->paintingAssistantsDecoration()->handles();
    m_canvas->updateCanvas();
}

void KisAssistantTool::loadAssistants()
{
    KoFileDialog dialog(m_canvas->viewManager()->mainWindow(), KoFileDialog::OpenFile, "OpenAssistant");
    dialog.setCaption(i18n("Select an Assistant"));
    dialog.setDefaultDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    dialog.setMimeTypeFilters(QStringList() << "application/x-krita-assistant", "application/x-krita-assistant");
    QString filename = dialog.filename();
    if (filename.isEmpty()) return;
    if (!QFileInfo(filename).exists()) return;

    QFile file(filename);
    file.open(QIODevice::ReadOnly);

    QByteArray data = file.readAll();
    QXmlStreamReader xml(data);
    QMap<int, KisPaintingAssistantHandleSP> handleMap;
    KisPaintingAssistantSP assistant;
    bool errors = false;
    while (!xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::StartElement:
            if (xml.name() == "handle") {
                if (assistant && !xml.attributes().value("ref").isEmpty()) {
                    KisPaintingAssistantHandleSP handle = handleMap.value(xml.attributes().value("ref").toString().toInt());
                    if (handle) {
                       assistant->addHandle(handle, HandleType::NORMAL);
                    } else {
                        errors = true;
                    }
                } else {
                    QString strId = xml.attributes().value("id").toString(),
                            strX = xml.attributes().value("x").toString(),
                            strY = xml.attributes().value("y").toString();
                    if (!strId.isEmpty() && !strX.isEmpty() && !strY.isEmpty()) {
                        int id = strId.toInt();
                        double x = strX.toDouble(),
                                y = strY.toDouble();
                        if (!handleMap.contains(id)) {
                            handleMap.insert(id, new KisPaintingAssistantHandle(x, y));
                        } else {
                            errors = true;
                        }
                    } else {
                        errors = true;
                    }
                }
            } else if (xml.name() == "assistant") {
                const KisPaintingAssistantFactory* factory = KisPaintingAssistantFactoryRegistry::instance()->get(xml.attributes().value("type").toString());
                if (factory) {
                    if (assistant) {
                        errors = true;
                        assistant.clear();
                    }
                    assistant = toQShared(factory->createPaintingAssistant());
                } else {
                    errors = true;
                }
            }
            break;
        case QXmlStreamReader::EndElement:
            if (xml.name() == "assistant") {
                if (assistant) {
                    if (assistant->handles().size() == assistant->numHandles()) {
                        if (assistant->id() == "vanishing point"){
                        //ideally we'd save and load side-handles as well, but this is all I've got//
                            QPointF pos = *assistant->handles()[0];
                            assistant->addHandle(new KisPaintingAssistantHandle(pos+QPointF(-70,0)), HandleType::SIDE);
                            assistant->addHandle(new KisPaintingAssistantHandle(pos+QPointF(-140,0)), HandleType::SIDE);
                            assistant->addHandle(new KisPaintingAssistantHandle(pos+QPointF(70,0)), HandleType::SIDE);
                            assistant->addHandle(new KisPaintingAssistantHandle(pos+QPointF(140,0)), HandleType::SIDE);
                        }
                        m_canvas->paintingAssistantsDecoration()->addAssistant(assistant);
                        KisAbstractPerspectiveGrid* grid = dynamic_cast<KisAbstractPerspectiveGrid*>(assistant.data());
                        if (grid) {
                            m_canvas->viewManager()->resourceProvider()->addPerspectiveGrid(grid);
                        }
                    } else {
                        errors = true;
                    }
                    assistant.clear();
                }
            }
            break;
        default:
            break;
        }
    }
    if (assistant) {
        errors = true;
        assistant.clear();
    }
    if (xml.hasError()) {
        QMessageBox::warning(0, i18nc("@title:window", "Krita"), xml.errorString());
    }
    if (errors) {
        QMessageBox::warning(0, i18nc("@title:window", "Krita"), i18n("Errors were encountered. Not all assistants were successfully loaded."));
    }
    m_handles = m_canvas->paintingAssistantsDecoration()->handles();
    m_canvas->updateCanvas();

}

void KisAssistantTool::saveAssistants()
{
    if (m_handles.isEmpty()) return;

    QByteArray data;
    QXmlStreamWriter xml(&data);
    xml.writeStartDocument();
    xml.writeStartElement("paintingassistant");
    xml.writeStartElement("handles");
    QMap<KisPaintingAssistantHandleSP, int> handleMap;
    Q_FOREACH (const KisPaintingAssistantHandleSP handle, m_handles) {
        int id = handleMap.size();
        handleMap.insert(handle, id);
        xml.writeStartElement("handle");
        //xml.writeAttribute("type", handle->handleType());
        xml.writeAttribute("id", QString::number(id));
        xml.writeAttribute("x", QString::number(double(handle->x()), 'f', 3));
        xml.writeAttribute("y", QString::number(double(handle->y()), 'f', 3));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeStartElement("assistants");
    Q_FOREACH (const KisPaintingAssistantSP assistant, m_canvas->paintingAssistantsDecoration()->assistants()) {
        xml.writeStartElement("assistant");
        xml.writeAttribute("type", assistant->id());
        xml.writeStartElement("handles");
        Q_FOREACH (const KisPaintingAssistantHandleSP handle, assistant->handles()) {
            xml.writeStartElement("handle");
            xml.writeAttribute("ref", QString::number(handleMap.value(handle)));
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();

    KoFileDialog dialog(m_canvas->viewManager()->mainWindow(), KoFileDialog::SaveFile, "OpenAssistant");
    dialog.setCaption(i18n("Save Assistant"));
    dialog.setDefaultDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    dialog.setMimeTypeFilters(QStringList() << "application/x-krita-assistant", "application/x-krita-assistant");
    QString filename = dialog.filename();
    if (filename.isEmpty()) return;

    QFile file(filename);
    file.open(QIODevice::WriteOnly);
    file.write(data);
}

QWidget *KisAssistantTool::createOptionWidget()
{
    if (!m_optionsWidget) {
        m_optionsWidget = new QWidget;
        m_options.setupUi(m_optionsWidget);

        // See https://bugs.kde.org/show_bug.cgi?id=316896
        QWidget *specialSpacer = new QWidget(m_optionsWidget);
        specialSpacer->setObjectName("SpecialSpacer");
        specialSpacer->setFixedSize(0, 0);
        m_optionsWidget->layout()->addWidget(specialSpacer);

        m_options.loadAssistantButton->setIcon(KisIconUtils::loadIcon("document-open"));
        m_options.saveAssistantButton->setIcon(KisIconUtils::loadIcon("document-save"));
        m_options.deleteAllAssistantsButton->setIcon(KisIconUtils::loadIcon("edit-delete"));

        QList<KoID> assistants;
        Q_FOREACH (const QString& key, KisPaintingAssistantFactoryRegistry::instance()->keys()) {
            QString name = KisPaintingAssistantFactoryRegistry::instance()->get(key)->name();
            assistants << KoID(key, name);
        }
        std::sort(assistants.begin(), assistants.end(), KoID::compareNames);
        Q_FOREACH(const KoID &id, assistants) {
            m_options.availableAssistantsComboBox->addItem(id.name(), id.id());
        }

        connect(m_options.saveAssistantButton, SIGNAL(clicked()), SLOT(saveAssistants()));
        connect(m_options.loadAssistantButton, SIGNAL(clicked()), SLOT(loadAssistants()));
        connect(m_options.deleteAllAssistantsButton, SIGNAL(clicked()), SLOT(removeAllAssistants()));

        connect(m_options.assistantsColor, SIGNAL(changed(const QColor&)), SLOT(slotAssistantsColorChanged(const QColor&)));
        connect(m_options.assistantsOpacitySlider, SIGNAL(sliderReleased()), SLOT(slotAssistantOpacityChanged()));


        m_options.assistantsColor->setColor(QColor(95, 228, 230, 255));

        // converts 10% to 0-255 range
        // todo: replace 1000 with opacity slider value
        m_options.assistantsOpacitySlider->setValue(100); // 30%
        m_assistantsOpacity = m_options.assistantsOpacitySlider->value()*0.01; // storing as 0-1 for the display, but QColor will eventually need 0-255

        QColor newColor = m_options.assistantsColor->color();
        newColor.setAlpha(m_assistantsOpacity*255);
        m_canvas->paintingAssistantsDecoration()->setAssistantsColor(newColor);
    }
    return m_optionsWidget;
}

void KisAssistantTool::slotAssistantsColorChanged(const QColor& setColor)
{
    // color and alpha are stored separately, so we need to merge the values before sending it on
    QColor newColor = setColor;
    newColor.setAlpha(m_assistantsOpacity * 255);
    m_canvas->paintingAssistantsDecoration()->setAssistantsColor(newColor);
    m_canvas->canvasWidget()->update();
}

void KisAssistantTool::slotAssistantOpacityChanged()
{
    QColor newColor = m_canvas->paintingAssistantsDecoration()->assistantsColor();
    m_assistantsOpacity = m_options.assistantsOpacitySlider->value()*0.01;
    newColor.setAlpha(m_assistantsOpacity*255);
    m_canvas->paintingAssistantsDecoration()->setAssistantsColor(newColor);
    m_canvas->canvasWidget()->update();
}


/*
 * functions only used interally in this class
 * we potentially could make some of these inline to speed up performance
*/

void KisAssistantTool::drawEditorWidget(KisPaintingAssistantSP assistant, QPainter& _gc)
{
    // We are going to put all of the assistant actions below the bounds of the assistant
    // so they are out of the way
    // assistant->buttonPosition() gets the center X/Y position point
    QPointF actionsPosition = m_canvas->viewConverter()->documentToView(assistant->buttonPosition());

    QPointF iconDeletePosition(actionsPosition + QPointF(78, m_assistantHelperYOffset + 7));
    QPointF iconSnapPosition(actionsPosition + QPointF(54, m_assistantHelperYOffset + 7));
    QPointF iconMovePosition(actionsPosition + QPointF(15, m_assistantHelperYOffset ));



    // Background container for helpers
    QBrush backgroundColor = m_canvas->viewManager()->mainWindow()->palette().window();
    QPointF actionsBGRectangle(actionsPosition + QPointF(25, m_assistantHelperYOffset));

    _gc.setRenderHint(QPainter::Antialiasing);

    QPainterPath bgPath;
    bgPath.addRoundedRect(QRectF(actionsBGRectangle.x(), actionsBGRectangle.y(), 80, 30), 6, 6);
    QPen stroke(QColor(60, 60, 60, 80), 2);
    _gc.setPen(stroke);
    _gc.fillPath(bgPath, backgroundColor);
    _gc.drawPath(bgPath);


    QPainterPath movePath;  // render circle behind by move helper
    _gc.setPen(stroke);
    movePath.addEllipse(iconMovePosition.x()-5, iconMovePosition.y()-5, 40, 40);// background behind icon
    _gc.fillPath(movePath, backgroundColor);
    _gc.drawPath(movePath);

    // Preview/Snap Tool helper
    _gc.drawPixmap(iconDeletePosition, m_iconDelete);
    if (assistant->isSnappingActive() == true) {
        _gc.drawPixmap(iconSnapPosition, m_iconSnapOn);
    }
    else {
        _gc.drawPixmap(iconSnapPosition, m_iconSnapOff);
    }

    // Move Assistant Tool helper
    _gc.drawPixmap(iconMovePosition, m_iconMove);
}

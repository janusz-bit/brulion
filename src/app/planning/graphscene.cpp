#include "graphscene.h"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <QSGVertexColorMaterial>
#include <QTextLayout>
#include <QTextOption>

#include <cmath>
#include <cstring>

namespace brulion::planning {

namespace {

/// Text layouts of labels not shown are kept for when they come back, up to this many.
constexpr size_t kMaxCachedLabels = 1024;

std::unique_ptr<QTextLayout> makeLayout(const QString &text, const QFont &font)
{
    auto layout = std::make_unique<QTextLayout>(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);
    layout->setTextOption(option);
    layout->setCacheEnabled(true);
    layout->beginLayout();
    QTextLine line = layout->createLine();
    if (line.isValid()) {
        line.setLineWidth(1e5);
        line.setPosition(QPointF(0, 0));
    }
    layout->endLayout();
    return layout;
}

} // namespace

GraphSceneNode::GraphSceneNode(QQuickWindow *window)
{
    m_background = window->createRectangleNode();
    appendChildNode(m_background);
    m_camera = new QSGTransformNode;
    appendChildNode(m_camera);
    m_labelRoot = new QSGNode;
    appendChildNode(m_labelRoot);
    m_labelBackgroundRoot = new QSGNode;
    m_labelRoot->appendChildNode(m_labelBackgroundRoot);
    m_labelTextRoot = new QSGNode;
    m_labelRoot->appendChildNode(m_labelTextRoot);
}

GraphSceneNode::~GraphSceneNode() = default; // child nodes are owned by their parents

void GraphSceneNode::setBackground(const QRectF &rect, const QColor &color)
{
    if (m_background->rect() != rect)
        m_background->setRect(rect);
    if (m_background->color() != color)
        m_background->setColor(color);
}

void GraphSceneNode::setCamera(const QMatrix4x4 &matrix)
{
    if (m_camera->matrix() != matrix)
        m_camera->setMatrix(matrix);
}

void GraphSceneNode::syncChunks(QSGNode *parent, std::vector<QSGGeometryNode *> &nodes, const geometry::MeshChunks &chunks)
{
    const auto meshes = chunks.chunks();
    while (nodes.size() > meshes.size()) {
        QSGGeometryNode *node = nodes.back();
        nodes.pop_back();
        parent->removeChildNode(node);
        delete node;
    }
    for (size_t i = 0; i < meshes.size(); ++i) {
        const geometry::Mesh &mesh = meshes[i];
        QSGGeometryNode *node = nullptr;
        if (i < nodes.size()) {
            node = nodes[i];
        } else {
            node = new QSGGeometryNode;
            auto *g = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0, 0, QSGGeometry::UnsignedShortType);
            g->setDrawingMode(QSGGeometry::DrawTriangles);
            g->setVertexDataPattern(QSGGeometry::StreamPattern);
            g->setIndexDataPattern(QSGGeometry::StreamPattern);
            node->setGeometry(g);
            node->setFlag(QSGNode::OwnsGeometry);
            node->setMaterial(new QSGVertexColorMaterial);
            node->setFlag(QSGNode::OwnsMaterial);
            parent->appendChildNode(node);
            nodes.push_back(node);
        }
        QSGGeometry *g = node->geometry();
        g->allocate(int(mesh.vertices.size()), int(mesh.indices.size()));
        static_assert(sizeof(geometry::Vertex) == sizeof(QSGGeometry::ColoredPoint2D));
        if (!mesh.vertices.empty())
            std::memcpy(g->vertexData(), mesh.vertices.data(), mesh.vertices.size() * sizeof(geometry::Vertex));
        if (!mesh.indices.empty())
            std::memcpy(g->indexData(), mesh.indices.data(), mesh.indices.size() * sizeof(quint16));
        node->markDirty(QSGNode::DirtyGeometry);
    }
}

void GraphSceneNode::setWorldMeshes(const geometry::MeshChunks &chunks)
{
    syncChunks(m_camera, m_world, chunks);
}

void GraphSceneNode::setLabelBackgrounds(const geometry::MeshChunks &chunks)
{
    syncChunks(m_labelBackgroundRoot, m_labelBackgrounds, chunks);
}

void GraphSceneNode::clearLabels()
{
    for (auto &[node, l] : m_labels) {
        if (l.node) {
            m_labelTextRoot->removeChildNode(l.node);
            delete l.node;
        }
    }
    m_labels.clear();
}

void GraphSceneNode::setLabels(QQuickWindow *window, const std::vector<Label> &labels, const QFont &font,
                               const QColor &color, const QColor &dimmedColor, qreal dpr)
{
    if (font != m_font) {
        clearLabels();
        m_font = font;
    }
    for (auto &[node, l] : m_labels)
        l.used = false;
    const qreal snap = dpr > 0 ? dpr : 1;
    for (const Label &label : labels) {
        LabelNode &l = m_labels[label.node];
        bool refill = false;
        if (!l.layout || l.text != label.text) {
            l.layout = makeLayout(label.text, m_font);
            l.text = label.text;
            refill = true;
        }
        if (!l.node) {
            l.node = window->createTextNode();
            m_labelTextRoot->appendChildNode(l.node);
            refill = true;
        }
        const QColor c = label.dimmed ? dimmedColor : color;
        if (refill || l.color != c) {
            l.node->clear();
            l.node->setColor(c);
            l.node->addTextLayout(QPointF(0, 0), l.layout.get());
            l.color = c;
        }
        QMatrix4x4 m;
        m.translate(float(std::round(label.pos.x() * snap) / snap), float(std::round(label.pos.y() * snap) / snap));
        if (l.node->matrix() != m)
            l.node->setMatrix(m);
        l.used = true;
    }
    // Labels not shown this frame lose their text node; their layouts stay cached (bounded).
    const bool trim = m_labels.size() > kMaxCachedLabels;
    for (auto it = m_labels.begin(); it != m_labels.end();) {
        LabelNode &l = it->second;
        if (l.used) {
            ++it;
            continue;
        }
        if (l.node) {
            m_labelTextRoot->removeChildNode(l.node);
            delete l.node;
            l.node = nullptr;
        }
        if (trim)
            it = m_labels.erase(it);
        else
            ++it;
    }
    m_shown = qsizetype(labels.size());
}

} // namespace brulion::planning

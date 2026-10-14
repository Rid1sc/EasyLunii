#include "app/editor/graphitems.h"

#include "core/studiopack.h"

#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QStyleOptionGraphicsItem>

static const QColor START_COLOR(38, 166, 154);    // teal
static const QColor STAGE_COLOR(66, 133, 244);
static const QColor ACTION_COLOR(171, 71, 188);
static const QColor HOME_COLOR(255, 167, 38);

// ------------------------------------------------------------------ nœud

NodeItem::NodeItem(Kind kind, const QString &id) : m_kind(kind), m_id(id)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setZValue(1);
}

QRectF NodeItem::boundingRect() const
{
    return QRectF(QPointF(-2, -2), m_size + QSizeF(4, 4));
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionChange && scene()) {
        // magnétisme sur une grille de 10 px
        QPointF p = value.toPointF();
        return QPointF(qRound(p.x() / 10) * 10, qRound(p.y() / 10) * 10);
    }
    if (change == ItemPositionHasChanged && scene()) {
        // mise à jour des liens attachés
        for (QGraphicsItem *it : scene()->items())
            if (auto *e = dynamic_cast<EdgeItem *>(it))
                if (e->from()->node() == this || e->to()->node() == this)
                    e->updatePath();
    }
    return QGraphicsObject::itemChange(change, value);
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent *e)
{
    m_pressPos = pos();
    QGraphicsObject::mousePressEvent(e);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *e)
{
    QGraphicsObject::mouseReleaseEvent(e);
    if (pos() != m_pressPos)
        emit moved(m_id, pos());   // connexion différée : la scène peut être reconstruite ensuite
}

static void drawCard(QPainter *p, const QRectF &r, const QColor &accent, bool selected, const QString &title)
{
    const QPalette pal = QApplication::palette();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(selected ? pal.color(QPalette::Highlight) : accent.darker(110), selected ? 2.5 : 1.2));
    p->setBrush(pal.color(QPalette::Base));
    p->drawRoundedRect(r, 8, 8);
    QPainterPath head;
    head.addRoundedRect(QRectF(r.left(), r.top(), r.width(), 24), 8, 8);
    head.addRect(QRectF(r.left(), r.top() + 12, r.width(), 12));
    p->fillPath(head.simplified(), accent);
    QFont f = QApplication::font();
    f.setBold(true);
    p->setFont(f);
    p->setPen(Qt::white);
    p->drawText(QRectF(r.left() + 8, r.top(), r.width() - 16, 24), Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(f).elidedText(title, Qt::ElideRight, int(r.width() - 16)));
}

// ------------------------------------------------------------------ nœud de scène

StageItem::StageItem(const lunii::StageNode &n, bool start, const QPixmap &image, const QString &audioLabel)
    : NodeItem(Stage, n.uuid), m_name(n.name), m_audio(audioLabel), m_image(image), m_start(start)
{
    m_size = {230, 128};
    const bool c[5] = {n.ctrl.wheel, n.ctrl.ok, n.ctrl.home, n.ctrl.pause, n.ctrl.autoplay};
    std::copy(c, c + 5, m_ctrl);
    m_in = new PortItem(PortItem::In, 0, this);
    m_in->setPos(-6, 58);
    m_ok = new PortItem(PortItem::Ok, 0, this);
    m_ok->setPos(m_size.width() - 6, 46);
    m_home = new PortItem(PortItem::Home, 0, this);
    m_home->setPos(m_size.width() - 6, 92);
    setPos(n.pos);
    setToolTip(n.name);
}

void StageItem::paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *)
{
    const QRectF r(QPointF(0, 0), m_size);
    drawCard(p, r, m_start ? START_COLOR : STAGE_COLOR, opt->state & QStyle::State_Selected,
             (m_start ? QString::fromUtf8("★ ") : QString()) + (m_name.isEmpty() ? QObject::tr("Scène") : m_name));
    const QPalette pal = QApplication::palette();
    // image (4:3)
    const QRectF img(8, 30, 88, 66);
    p->setPen(QPen(pal.color(QPalette::Mid), 1));
    p->setBrush(pal.color(QPalette::AlternateBase));
    p->drawRect(img);
    if (!m_image.isNull())
        p->drawPixmap(img.toRect(), m_image);
    else {
        p->setPen(pal.color(QPalette::PlaceholderText));
        p->drawText(img, Qt::AlignCenter, QObject::tr("pas d'image"));
    }
    // audio
    QFont f = QApplication::font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p->setFont(f);
    p->setPen(m_audio.isEmpty() ? pal.color(QPalette::PlaceholderText) : pal.color(QPalette::Text));
    p->drawText(QRectF(104, 30, r.width() - 116, 40), Qt::AlignLeft | Qt::TextWordWrap,
                m_audio.isEmpty() ? QObject::tr("♪ pas de son") : QString::fromUtf8("♪ ") + m_audio);
    // commandes actives
    static const char *labels[5] = {"⟲", "OK", "⌂", "❚❚", "▶"};
    static const char *tips[5] = {"molette", "OK", "maison", "pause", "auto"};
    Q_UNUSED(tips);
    qreal x = 104;
    for (int i = 0; i < 5; ++i) {
        const QString t = QString::fromUtf8(labels[i]);
        const qreal w = QFontMetrics(f).horizontalAdvance(t) + 8;
        const QRectF b(x, 76, w, 16);
        p->setPen(Qt::NoPen);
        p->setBrush(m_ctrl[i] ? STAGE_COLOR.lighter(130) : pal.color(QPalette::AlternateBase));
        p->drawRoundedRect(b, 4, 4);
        p->setPen(m_ctrl[i] ? Qt::white : pal.color(QPalette::PlaceholderText));
        p->drawText(b, Qt::AlignCenter, t);
        x += w + 3;
    }
    // libellés des sorties
    p->setPen(pal.color(QPalette::PlaceholderText));
    p->drawText(QRectF(r.width() - 60, 38, 48, 16), Qt::AlignRight | Qt::AlignVCenter, "OK");
    p->drawText(QRectF(r.width() - 70, 84, 58, 16), Qt::AlignRight | Qt::AlignVCenter, QObject::tr("Maison"));
}

// ------------------------------------------------------------------ nœud d'action

ActionItem::ActionItem(const lunii::ActionNode &n, const QStringList &optionNames)
    : NodeItem(Action, n.id), m_name(n.name), m_optionNames(optionNames)
{
    m_size = {190, 34.0 + 22 * (optionNames.size() + 1)};
    m_in = new PortItem(PortItem::In, 0, this);
    m_in->setPos(-6, 12);
    for (int i = 0; i < optionNames.size(); ++i) {
        auto *pt = new PortItem(PortItem::Option, i, this);
        pt->setPos(m_size.width() - 6, 30 + 22 * i + 5);
        m_options << pt;
    }
    m_add = new PortItem(PortItem::AddOption, int(optionNames.size()), this);
    m_add->setPos(m_size.width() - 6, 30 + 22 * optionNames.size() + 5);
    setPos(n.pos);
}

void ActionItem::paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *)
{
    const QRectF r(QPointF(0, 0), m_size);
    drawCard(p, r, ACTION_COLOR, opt->state & QStyle::State_Selected,
             m_name.isEmpty() ? QObject::tr("Choix") : m_name);
    const QPalette pal = QApplication::palette();
    QFont f = QApplication::font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p->setFont(f);
    const QFontMetrics fm(f);
    for (int i = 0; i < m_optionNames.size(); ++i) {
        p->setPen(pal.color(QPalette::Text));
        p->drawText(QRectF(10, 28 + 22 * i, r.width() - 26, 22), Qt::AlignVCenter,
                    fm.elidedText(QString("%1. %2").arg(i + 1).arg(m_optionNames[i]), Qt::ElideRight, int(r.width() - 26)));
    }
    p->setPen(pal.color(QPalette::PlaceholderText));
    p->drawText(QRectF(10, 28 + 22 * m_optionNames.size(), r.width() - 26, 22), Qt::AlignVCenter,
                QObject::tr("+ tirer vers une scène"));
}

// ------------------------------------------------------------------ ports et liens

PortItem::PortItem(Role role, int index, NodeItem *parent) : QGraphicsEllipseItem(0, 0, 12, 12, parent), m_role(role), m_index(index)
{
    QColor c = role == Home ? HOME_COLOR : role == In ? QColor(120, 120, 120) : role == AddOption ? QColor(150, 150, 150) : STAGE_COLOR;
    if (role == Option)
        c = ACTION_COLOR;
    setBrush(c);
    setPen(QPen(Qt::white, 1.5));
    setZValue(2);
    setCursor(role == In ? Qt::ArrowCursor : Qt::CrossCursor);
    setToolTip(role == Ok ? QObject::tr("Sortie OK : tirer vers un choix ou une scène")
             : role == Home ? QObject::tr("Sortie Maison : tirer vers un choix ou une scène")
             : role == AddOption ? QObject::tr("Ajouter une option : tirer vers une scène")
             : role == Option ? QObject::tr("Option %1").arg(index + 1) : QString());
}

EdgeItem::EdgeItem(Type type, PortItem *from, PortItem *to, const QString &label)
    : m_type(type), m_from(from), m_to(to), m_label(label)
{
    QColor c = type == HomeEdge ? HOME_COLOR : type == OptionEdge ? ACTION_COLOR : STAGE_COLOR;
    QPen pen(c, 2);
    if (type == HomeEdge) {
        // retours « maison » : nombreux et souvent longs, donc discrets
        c.setAlpha(90);
        pen = QPen(c, 1.5, Qt::DashLine);
    }
    setPen(pen);
    setZValue(0);
    setFlag(ItemIsSelectable);
    updatePath();
}

void EdgeItem::updatePath()
{
    const QPointF a = m_from->anchor(), b = m_to->anchor();
    const qreal dx = qMax<qreal>(60, qAbs(b.x() - a.x()) / 2);
    QPainterPath path(a);
    path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
    prepareGeometryChange();
    setPath(path);
}

QPainterPath EdgeItem::shape() const
{
    QPainterPathStroker s;
    s.setWidth(10);
    return s.createStroke(path());
}

void EdgeItem::paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *)
{
    p->setRenderHint(QPainter::Antialiasing);
    QPen pen = this->pen();
    if (opt->state & QStyle::State_Selected) {
        pen.setWidthF(3.5);
        pen.setColor(QApplication::palette().color(QPalette::Highlight));
    }
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawPath(path());
    // flèche
    const QPointF tip = path().pointAtPercent(1.0), before = path().pointAtPercent(0.97);
    const QLineF dir(tip, before);
    QLineF l1 = dir, l2 = dir;
    l1.setLength(10);
    l2.setLength(10);
    l1.setAngle(dir.angle() + 25);
    l2.setAngle(dir.angle() - 25);
    p->drawLine(l1);
    p->drawLine(l2);
    if (!m_label.isEmpty()) {
        const QPointF mid = path().pointAtPercent(0.5);
        QFont f = QApplication::font();
        f.setPointSizeF(f.pointSizeF() * 0.8);
        p->setFont(f);
        const QRectF br = QFontMetricsF(f).boundingRect(m_label).adjusted(-4, -1, 4, 1).translated(mid - QPointF(0, 0));
        QRectF box(mid.x() - br.width() / 2, mid.y() - br.height() / 2, br.width(), br.height());
        p->setPen(Qt::NoPen);
        p->setBrush(QApplication::palette().color(QPalette::Base));
        p->drawRoundedRect(box, 4, 4);
        p->setPen(pen.color());
        p->drawText(box, Qt::AlignCenter, m_label);
    }
}

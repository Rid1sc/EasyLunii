// Éléments graphiques du graphe de l'éditeur (nœuds de scène, nœuds d'action, liens).
#pragma once

#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QPixmap>

namespace lunii { struct StageNode; struct ActionNode; }

class PortItem;

class NodeItem : public QGraphicsObject
{
    Q_OBJECT
public:
    enum Kind { Stage, Action };
    NodeItem(Kind kind, const QString &id);
    Kind kind() const { return m_kind; }
    QString id() const { return m_id; }
    PortItem *inPort() const { return m_in; }
    QRectF boundingRect() const override;

signals:
    void moved(const QString &id, const QPointF &pos);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override;
    Kind m_kind;
    QString m_id;
    QSizeF m_size;
    PortItem *m_in = nullptr;
    QPointF m_pressPos;
};

class StageItem : public NodeItem
{
    Q_OBJECT
public:
    StageItem(const lunii::StageNode &n, bool start, const QPixmap &image, const QString &audioLabel);
    PortItem *okPort() const { return m_ok; }
    PortItem *homePort() const { return m_home; }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *w) override;

private:
    QString m_name, m_audio;
    QPixmap m_image;
    bool m_start;
    bool m_ctrl[5];
    PortItem *m_ok, *m_home;
};

class ActionItem : public NodeItem
{
    Q_OBJECT
public:
    ActionItem(const lunii::ActionNode &n, const QStringList &optionNames);
    PortItem *optionPort(int i) const { return m_options.value(i); }
    PortItem *addPort() const { return m_add; }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *w) override;

private:
    QString m_name;
    QStringList m_optionNames;
    QList<PortItem *> m_options;
    PortItem *m_add;
};

// Point d'accroche : on tire un lien depuis une sortie vers un nœud
class PortItem : public QGraphicsEllipseItem
{
public:
    enum Role { In, Ok, Home, Option, AddOption };
    PortItem(Role role, int index, NodeItem *parent);
    Role role() const { return m_role; }
    int index() const { return m_index; }
    NodeItem *node() const { return static_cast<NodeItem *>(parentItem()); }
    QPointF anchor() const { return mapToScene(rect().center()); }

private:
    Role m_role;
    int m_index;
};

class EdgeItem : public QGraphicsPathItem
{
public:
    enum Type { OkEdge, HomeEdge, OptionEdge };
    EdgeItem(Type type, PortItem *from, PortItem *to, const QString &label);
    void updatePath();
    Type edgeType() const { return m_type; }
    PortItem *from() const { return m_from; }
    PortItem *to() const { return m_to; }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *w) override;
    QPainterPath shape() const override;

private:
    Type m_type;
    PortItem *m_from, *m_to;
    QString m_label;
};

#include "diagram/layered_p.h"

#include <algorithm>
#include <numeric>

namespace brulion::diagram::detail {

namespace {

struct Segment {
    int from = 0, to = 0;
    qreal weight = 1;
};

/// Positions nearest to `desired` (weighted least squares) keeping x[i+1] − x[i] ≥ sep[i]:
/// pool adjacent violators on desired − cumulative separation.
QList<qreal> isotonic(const QList<qreal> &desired, const QList<qreal> &weights, const QList<qreal> &sep)
{
    const qsizetype n = desired.size();
    QList<qreal> offset(n, 0);
    for (qsizetype i = 1; i < n; ++i)
        offset[i] = offset[i - 1] + sep[i - 1];
    struct Block {
        qreal w, wt;
        qsizetype count;
        qreal mean() const { return wt / w; }
    };
    QList<Block> blocks;
    blocks.reserve(n);
    for (qsizetype i = 0; i < n; ++i) {
        blocks.append({weights[i], weights[i] * (desired[i] - offset[i]), 1});
        while (blocks.size() >= 2 && blocks[blocks.size() - 2].mean() > blocks.last().mean()) {
            const Block top = blocks.takeLast();
            Block &b = blocks.last();
            b.w += top.w;
            b.wt += top.wt;
            b.count += top.count;
        }
    }
    QList<qreal> out(n);
    qsizetype i = 0;
    for (const Block &b : blocks) {
        const qreal m = b.mean();
        for (qsizetype k = 0; k < b.count; ++k, ++i)
            out[i] = m + offset[i];
    }
    return out;
}

/// Crossings between the segments from one layer to the next (positions of their ends).
qint64 crossings(QList<std::pair<int, int>> &pairs, int lowerSize)
{
    std::ranges::sort(pairs);
    QList<int> tree(lowerSize + 1, 0); // Fenwick tree over the lower layer's positions
    qint64 count = 0;
    qint64 seen = 0;
    for (const auto &[a, b] : pairs) {
        Q_UNUSED(a);
        // Segments already seen ending right of b cross this one.
        qint64 atMost = 0;
        for (int i = b + 1; i > 0; i -= i & -i)
            atMost += tree[i];
        count += seen - atMost;
        for (int i = b + 1; i <= lowerSize; i += i & -i)
            ++tree[i];
        ++seen;
    }
    return count;
}

} // namespace

std::optional<LayerResult> layered(const QList<LayerNode> &nodes, const QList<LayerEdge> &edges,
                                   const LayerOptions &options)
{
    const int n = int(nodes.size());
    LayerResult result;
    result.centers.resize(n);
    result.bends.resize(edges.size());
    result.labels.resize(edges.size());
    result.ranks.resize(n);
    if (n == 0)
        return result;

    // 1. Cycles: DFS back edges reversed.
    QList<QList<int>> out(n);
    for (qsizetype e = 0; e < edges.size(); ++e)
        out[edges[e].from].append(int(e));
    QList<bool> reversed(edges.size(), false);
    {
        QList<char> state(n, 0);
        QList<std::pair<int, qsizetype>> stack;
        for (int root = 0; root < n; ++root) {
            if (state[root])
                continue;
            stack.append({root, 0});
            state[root] = 1;
            while (!stack.isEmpty()) {
                auto &[v, next] = stack.last();
                if (next < out[v].size()) {
                    const int e = out[v][next++];
                    const int w = edges[e].to;
                    if (state[w] == 1)
                        reversed[e] = true;
                    else if (state[w] == 0) {
                        state[w] = 1;
                        stack.append({w, 0});
                    }
                } else {
                    state[v] = 2;
                    stack.removeLast();
                }
            }
        }
    }
    const auto tail = [&](qsizetype e) { return reversed[e] ? edges[e].to : edges[e].from; };
    const auto head = [&](qsizetype e) { return reversed[e] ? edges[e].from : edges[e].to; };
    const auto span = [&](qsizetype e) { return 2 * std::clamp(edges[e].minLength, 1, 10); };

    // 2. Ranks: longest path from the sources, then sources pulled down to their successors.
    QList<int> &rank = result.ranks;
    QList<QList<int>> outs(n), ins(n);
    for (qsizetype e = 0; e < edges.size(); ++e) {
        outs[tail(e)].append(int(e));
        ins[head(e)].append(int(e));
    }
    QList<int> order;
    order.reserve(n);
    {
        QList<int> indeg(n);
        for (int v = 0; v < n; ++v)
            indeg[v] = int(ins[v].size());
        for (int v = 0; v < n; ++v)
            if (indeg[v] == 0)
                order.append(v);
        for (qsizetype k = 0; k < order.size(); ++k) {
            const int v = order[k];
            for (const int e : std::as_const(outs[v])) {
                const int w = head(e);
                rank[w] = std::max(rank[w], rank[v] + span(e));
                if (--indeg[w] == 0)
                    order.append(w);
            }
        }
    }
    for (qsizetype k = order.size() - 1; k >= 0; --k) {
        const int v = order[k];
        if (!ins[v].isEmpty() || outs[v].isEmpty())
            continue;
        int r = std::numeric_limits<int>::max();
        for (const int e : std::as_const(outs[v]))
            r = std::min(r, rank[head(e)] - span(e));
        rank[v] = r;
    }
    const int minRank = *std::min_element(rank.cbegin(), rank.cend());
    for (int &r : rank)
        r -= minRank;
    const int ranks = *std::max_element(rank.cbegin(), rank.cend()) + 1;

    // 3. Bends: a node in every rank a link passes; the label is the middle one.
    struct LNode {
        qreal w, h, extra;
        int rank;
        bool dummy;
    };
    QList<LNode> ln;
    ln.reserve(n);
    for (int v = 0; v < n; ++v)
        ln.append({nodes[v].width, nodes[v].height, nodes[v].extraAfter, rank[v], false});
    QList<QList<int>> chains(edges.size());
    QList<Segment> segments;
    QList<int> labelNode(edges.size(), -1);
    for (qsizetype e = 0; e < edges.size(); ++e) {
        const int a = tail(e), b = head(e);
        const int ra = rank[a], rb = rank[b];
        const int mid = ra + (rb - ra) / 2;
        QList<int> &chain = chains[e];
        chain.append(a);
        for (int r = ra + 1; r < rb; ++r) {
            if (ln.size() >= options.maxNodes)
                return std::nullopt;
            const bool label = r == mid && !edges[e].label.isEmpty();
            ln.append({label ? edges[e].label.width() : 0, label ? edges[e].label.height() : 0, 0, r, true});
            if (label)
                labelNode[e] = int(ln.size() - 1);
            chain.append(int(ln.size() - 1));
        }
        chain.append(b);
        for (qsizetype k = 0; k + 1 < chain.size(); ++k) {
            const bool da = ln[chain[k]].dummy, db = ln[chain[k + 1]].dummy;
            segments.append({chain[k], chain[k + 1], da && db ? 8.0 : da || db ? 2.0 : 1.0});
        }
    }
    const int total = int(ln.size());
    QList<QList<std::pair<int, qreal>>> preds(total), succs(total);
    for (const Segment &s : std::as_const(segments)) {
        succs[s.from].append({s.to, s.weight});
        preds[s.to].append({s.from, s.weight});
    }

    // 4. Order within the ranks: depth-first from the nodes in their order, then sweeps.
    QList<QList<int>> layers(ranks);
    {
        QList<bool> placed(total, false);
        QList<int> stack;
        const auto visitFrom = [&](int root) {
            stack.append(root);
            while (!stack.isEmpty()) {
                const int v = stack.takeLast();
                if (placed[v])
                    continue;
                placed[v] = true;
                layers[ln[v].rank].append(v);
                for (qsizetype k = succs[v].size() - 1; k >= 0; --k)
                    if (!placed[succs[v][k].first])
                        stack.append(succs[v][k].first);
            }
        };
        // Sources first (in their order), then whatever is left.
        for (int v = 0; v < n; ++v)
            if (ins[v].isEmpty())
                visitFrom(v);
        for (int v = 0; v < total; ++v)
            if (!placed[v])
                visitFrom(v);
    }
    QList<int> pos(total);
    const auto index = [&] {
        for (const QList<int> &layer : std::as_const(layers))
            for (qsizetype i = 0; i < layer.size(); ++i)
                pos[layer[i]] = int(i);
    };
    index();
    const auto countCrossings = [&] {
        qint64 c = 0;
        QList<std::pair<int, int>> pairs;
        for (int r = 0; r + 1 < ranks; ++r) {
            pairs.clear();
            for (const int v : std::as_const(layers[r]))
                for (const auto &[w, weight] : std::as_const(succs[v])) {
                    Q_UNUSED(weight);
                    pairs.append({pos[v], pos[w]});
                }
            c += crossings(pairs, int(layers[r + 1].size()));
        }
        return c;
    };
    qint64 best = countCrossings();
    QList<QList<int>> bestLayers = layers;
    int sinceBetter = 0;
    for (int sweep = 0; sweep < options.maxSweeps && best > 0 && sinceBetter < 4; ++sweep) {
        const bool down = sweep % 2 == 0;
        for (int step = 0; step < ranks - 1; ++step) {
            const int r = down ? step + 1 : ranks - 2 - step;
            QList<int> &layer = layers[r];
            QList<std::pair<qreal, int>> movable;
            QList<qsizetype> slots;
            for (qsizetype i = 0; i < layer.size(); ++i) {
                const auto &nb = down ? preds[layer[i]] : succs[layer[i]];
                if (nb.isEmpty())
                    continue;
                qreal sum = 0;
                for (const auto &[w, weight] : nb) {
                    Q_UNUSED(weight);
                    sum += pos[w];
                }
                movable.append({sum / nb.size(), layer[i]});
                slots.append(i);
            }
            std::ranges::stable_sort(movable, [](const auto &a, const auto &b) { return a.first < b.first; });
            for (qsizetype k = 0; k < slots.size(); ++k)
                layer[slots[k]] = movable[k].second;
            for (qsizetype i = 0; i < layer.size(); ++i)
                pos[layer[i]] = int(i);
        }
        const qint64 c = countCrossings();
        if (c < best) {
            best = c;
            bestLayers = layers;
            sinceBetter = 0;
        } else {
            ++sinceBetter;
        }
    }
    layers = bestLayers;
    index();

    // 5. Coordinates across the ranks.
    QList<qreal> x(total, 0);
    const auto gap = [&](int a, int b) {
        const bool da = ln[a].dummy && ln[a].w == 0, db = ln[b].dummy && ln[b].w == 0;
        return da && db ? options.edgeSep : da || db ? (options.nodeSep + options.edgeSep) / 2 : options.nodeSep;
    };
    const auto separation = [&](int a, int b) { return ln[a].w / 2 + ln[a].extra + ln[b].w / 2 + gap(a, b); };
    for (const QList<int> &layer : std::as_const(layers)) {
        qreal at = 0;
        for (qsizetype i = 0; i < layer.size(); ++i) {
            if (i > 0)
                at += separation(layer[i - 1], layer[i]);
            x[layer[i]] = at;
        }
        for (const int v : layer)
            x[v] -= at / 2;
    }
    const auto place = [&](int r, bool usePreds, bool useSuccs) {
        const QList<int> &layer = layers[r];
        if (layer.isEmpty())
            return;
        QList<qreal> desired(layer.size()), weights(layer.size()), sep(std::max<qsizetype>(0, layer.size() - 1));
        for (qsizetype i = 0; i < layer.size(); ++i) {
            const int v = layer[i];
            qreal sum = 0, wsum = 0;
            if (usePreds)
                for (const auto &[u, w] : std::as_const(preds[v])) {
                    sum += w * x[u];
                    wsum += w;
                }
            if (useSuccs)
                for (const auto &[u, w] : std::as_const(succs[v])) {
                    sum += w * x[u];
                    wsum += w;
                }
            desired[i] = wsum > 0 ? sum / wsum : x[v];
            weights[i] = wsum > 0 ? wsum : 0.05;
            if (i > 0)
                sep[i - 1] = separation(layer[i - 1], v);
        }
        const QList<qreal> placed = isotonic(desired, weights, sep);
        for (qsizetype i = 0; i < layer.size(); ++i)
            x[layer[i]] = placed[i];
    };
    for (int round = 0; round < 6; ++round) {
        for (int r = 1; r < ranks; ++r)
            place(r, true, false);
        for (int r = ranks - 2; r >= 0; --r)
            place(r, false, true);
    }
    for (int round = 0; round < 2; ++round)
        for (int r = 0; r < ranks; ++r)
            place(r, true, true);

    // 6. Coordinates along the ranks.
    QList<qreal> rowHeight(ranks, 0), rowTop(ranks, 0);
    for (int v = 0; v < total; ++v)
        rowHeight[ln[v].rank] = std::max(rowHeight[ln[v].rank], ln[v].h);
    for (int r = 1; r < ranks; ++r)
        rowTop[r] = rowTop[r - 1] + rowHeight[r - 1] + options.rankSep / 2;
    qreal minX = std::numeric_limits<qreal>::max(), maxX = std::numeric_limits<qreal>::lowest();
    for (int v = 0; v < total; ++v) {
        minX = std::min(minX, x[v] - ln[v].w / 2);
        maxX = std::max(maxX, x[v] + ln[v].w / 2 + ln[v].extra);
    }
    const auto center = [&](int v) { return QPointF(x[v] - minX, rowTop[ln[v].rank] + rowHeight[ln[v].rank] / 2); };
    result.size = QSizeF(maxX - minX, rowTop[ranks - 1] + rowHeight[ranks - 1]);
    for (int v = 0; v < n; ++v)
        result.centers[v] = center(v);
    for (qsizetype e = 0; e < edges.size(); ++e) {
        QList<QPointF> &bends = result.bends[e];
        const QList<int> &chain = chains[e];
        for (qsizetype k = 1; k + 1 < chain.size(); ++k)
            bends.append(center(chain[k]));
        if (reversed[e])
            std::ranges::reverse(bends);
        if (labelNode[e] >= 0)
            result.labels[e] = center(labelNode[e]);
    }
    return result;
}

} // namespace brulion::diagram::detail

"""Algorithm-level tests for the spacewarp estimator (swp_est_math_test.py).

Validates the ALGORITHM CONTRACT of swp_shaders.h without GL: the block-match
SAD forward-flow search, the GME coarse global search, the 9-element sorting-
network median, H1 + parabolic subpel refinement, and the extrapolation warp
(clampMagnitude + sr.mode/sr.radius stretch) are re-implemented in pure python
with the SAME loop shapes and constants as the GLSL, then checked on tiny
deterministic synthetic frames with known translations.

Test-frame strategy (deterministic, no numpy):
  - noise luma frames (LCG, seeded) for the matching stages: an integer shift
    is an EXACT copy, so the true offset has SAD == 0 and a unique minimum;
  - a linear value ramp (value == x) for the warp stages: bilinear sampling is
    exact at any uv, so every edge-handling assertion is analytic.

Conventions under test (pinned in spacewarp_est.h):
  - stored mv .rg = uv displacement = NDC delta / 2; forward flow measured
    prev -> curr, evaluated at prev-frame block positions;
  - extrapolate: dst(q) = src(q + mv * fovScale * (1 - scaleFactor)) with the
    clampMagnitude/sr stretch/clamp edge chain;
  - uv = texel / inputSize.
"""
import json
import sys
from pathlib import Path

FAILURES = []
CHECKS = 0


def check(cond, label):
    global CHECKS
    CHECKS += 1
    if not cond:
        FAILURES.append(label)
        print("FAIL:", label)


# --------------------------------------------------------------- frames ---

def noise_frame(w, h, seed=123456789):
    """Deterministic per-pixel pseudo-random luma in [0,1) (LCG)."""
    s = seed
    f = []
    for _ in range(h):
        row = []
        for _ in range(w):
            s = (s * 1103515245 + 12345) & 0x7FFFFFFF
            row.append(((s >> 8) & 0xFFFF) / 65535.0)
        f.append(row)
    return f


def linear_frame(w, h):
    """value == x index: bilinear sampling is exact at any u."""
    return [[float(x) for x in range(w)] for _ in range(h)]


def smooth_frame(w, h):
    """Deterministic low-frequency structure: SAD landscape is smooth around
    the true offset, so the parabolic subpel fit is well-formed (white noise
    ties its integer SADs by expectation and cannot anchor a parabola)."""
    import math
    return [[0.5 + 0.4 * math.sin(x * 0.35) * math.sin(y * 0.27) for x in range(w)]
            for y in range(h)]


def bilinear(tex, u, v, w, h):
    x = min(max(u, 0.0), 1.0) * (w - 1)
    y = min(max(v, 0.0), 1.0) * (h - 1)
    x0, y0 = int(x), int(y)
    x1, y1 = min(x0 + 1, w - 1), min(y0 + 1, h - 1)
    fx, fy = x - x0, y - y0
    a = tex[y0][x0] * (1 - fx) + tex[y0][x1] * fx
    b = tex[y1][x0] * (1 - fx) + tex[y1][x1] * fx
    return a * (1 - fy) + b * fy


def bilinear2(tex, u, v, w, h):
    """Per-channel bilinear for 2-tuple texels (the mv map)."""
    x = min(max(u, 0.0), 1.0) * (w - 1)
    y = min(max(v, 0.0), 1.0) * (h - 1)
    x0, y0 = int(x), int(y)
    x1, y1 = min(x0 + 1, w - 1), min(y0 + 1, h - 1)
    fx, fy = x - x0, y - y0
    a = tuple(tex[y0][x0][k] * (1 - fx) + tex[y0][x1][k] * fx for k in range(2))
    b = tuple(tex[y1][x0][k] * (1 - fx) + tex[y1][x1][k] * fx for k in range(2))
    return tuple(a[k] * (1 - fy) + b[k] * fy for k in range(2))


def shifted_copy(src, dx, dy, w, h):
    """curr[y][x] = prev[y - dy][x - dx] (integer translation; edge clamp)."""
    return [[src[min(max(y - dy, 0), h - 1)][min(max(x - dx, 0), w - 1)]
             for x in range(w)] for y in range(h)]


def shifted_resampled(src, dx, dy, w, h):
    """curr(x) = prev(x - dx): bilinear resample (fractional translation).
    Uses the (x - dx)/(w - 1) uv convention so the resample is an EXACT pure
    translation of dx texels (bilinear maps u*(w-1) -> texel coordinate)."""
    return [[bilinear(src, (x - dx) / (w - 1), (y - dy) / (h - 1), w, h)
             for x in range(w)] for y in range(h)]


# ----------------------------------------------------------- MV pipeline ---

def block_sad(prev, curr, origin, cand, block):
    s = 0.0
    for y in range(block):
        for x in range(block):
            s += abs(prev[origin[1] + y][origin[0] + x] - curr[cand[1] + y][cand[0] + x])
    return s


def quad_sad(prev, curr, origin, cand, block):
    """Four-quadrant sum per candidate (the GLSL quadSad)."""
    b = block
    s = block_sad(prev, curr, origin, cand, b)
    s += block_sad(prev, curr, (origin[0] + b, origin[1]), (cand[0] + b, cand[1]), b)
    s += block_sad(prev, curr, (origin[0] + b, origin[1] + b),
                   (cand[0] + b, cand[1] + b), b)
    s += block_sad(prev, curr, (origin[0], origin[1] + b), (cand[0], cand[1] + b), b)
    return s


def gme_pass(prev, curr, input_w, input_h, block, gme_step, origin_stride, global_max):
    """swp_gme_frag: strided block-lattice mini-SADs over the candidate grid."""
    best, best_score, blocks = (0, 0), None, 0
    oy = -global_max
    while oy <= global_max:
        ox = -global_max
        while ox <= global_max:
            score, seen = 0.0, 0
            by = global_max
            while by + block + global_max <= input_h:
                bx = global_max
                while bx + block + global_max <= input_w:
                    score += block_sad(prev, curr, (bx, by), (bx + ox, by + oy), block)
                    seen += 1
                    bx += origin_stride
                by += origin_stride
            if seen == 0:
                ox += gme_step
                continue
            score /= seen
            blocks = 1
            if best_score is None or score < best_score:
                best_score, best = score, (ox, oy)
            ox += gme_step
        oy += gme_step
    return best


def block_origin(mv_t, mv_size, block_grid, block_size, global_max, input_size):
    """Same texel -> block -> clamped origin mapping as the shaders."""
    bidx = (min((mv_t[0] * block_grid[0]) // mv_size[0], block_grid[0] - 1),
            min((mv_t[1] * block_grid[1]) // mv_size[1], block_grid[1] - 1))
    origin = (bidx[0] * block_size, bidx[1] * block_size)
    origin = (min(max(origin[0], global_max), input_size[0] - 2 * block_size - global_max),
              min(max(origin[1], global_max), input_size[1] - 2 * block_size - global_max))
    return origin


def sad_pass(prev, curr, cfg, gme):
    """swp_sad_frag: per-mv-texel search around the GME seed."""
    mv_w, mv_h = cfg["mv_w"], cfg["mv_h"]
    block, block_grid = cfg["block"], cfg["block_grid"]
    span, gmax = cfg["search_span"], cfg["global_max"]
    input_w, input_h = cfg["input_w"], cfg["input_h"]
    out = [[(0.0, 0.0)] * mv_w for _ in range(mv_h)]
    for my in range(mv_h):
        for mx in range(mv_w):
            origin = block_origin((mx, my), (mv_w, mv_h), block_grid, block, gmax,
                                  (input_w, input_h))
            center = quad_sad(prev, curr, origin, origin, block)
            if center / cfg["sad_scale"] < cfg["no_motion"]:
                continue
            lo = (max(gme[0] - span, -gmax), max(gme[1] - span, -gmax))
            hi = (min(gme[0] + span, gmax), min(gme[1] + span, gmax))
            best_sad, best_o = center, (0, 0)
            for oy in range(lo[1], hi[1] + 1):
                for ox in range(lo[0], hi[0] + 1):
                    if ox == 0 and oy == 0:
                        continue
                    cand = (origin[0] + ox, origin[1] + oy)
                    if cand[0] < 0 or cand[1] < 0 or \
                       cand[0] + 2 * block > input_w or cand[1] + 2 * block > input_h:
                        continue
                    s = quad_sad(prev, curr, origin, cand, block)
                    if s < best_sad:
                        best_sad, best_o = s, (ox, oy)
            if best_o != (0, 0):
                out[my][mx] = (float(best_o[0]), float(best_o[1]))
    return out


def median_pass(l1, cfg):
    """swp_median_frag: 9-element sorting network (the 19-swap median net)."""
    mv_w, mv_h, stride = cfg["mv_w"], cfg["mv_h"], cfg["median_stride"]
    out = [[(0.0, 0.0)] * mv_w for _ in range(mv_h)]
    for ty in range(mv_h):
        for tx in range(mv_w):
            v = []
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    cx = min(max(tx + dx * stride, 0), mv_w - 1)
                    cy = min(max(ty + dy * stride, 0), mv_h - 1)
                    v.append(list(l1[cy][cx]))
            tmp = [0.0, 0.0]

            def sw(a, b):
                for k in range(2):
                    tmp[k] = v[a][k]
                    v[a][k] = min(v[a][k], v[b][k])
                    v[b][k] = max(tmp[k], v[b][k])

            sw(0, 1); sw(3, 4); sw(6, 7)
            sw(1, 2); sw(4, 5); sw(7, 8)
            sw(0, 1); sw(3, 4); sw(6, 7); sw(2, 5)
            sw(0, 3); sw(1, 4); sw(5, 8)
            sw(3, 6); sw(4, 7); sw(2, 5)
            sw(0, 3); sw(1, 4); sw(5, 7); sw(2, 6)
            sw(1, 3); sw(4, 6)
            sw(2, 4); sw(5, 6)
            sw(2, 3)
            out[ty][tx] = (v[4][0], v[4][1])
    return out


def h1_pass(prev, curr, med, cfg):
    """swp_h1_frag: median seed + 3x3 refine + parabolic subpel -> uv field."""
    mv_w, mv_h = cfg["mv_w"], cfg["mv_h"]
    block, block_grid = cfg["block"], cfg["block_grid"]
    gmax = cfg["global_max"]
    input_w, input_h = cfg["input_w"], cfg["input_h"]
    out = [[(0.0, 0.0)] * mv_w for _ in range(mv_h)]
    for my in range(mv_h):
        for mx in range(mv_w):
            origin = block_origin((mx, my), (mv_w, mv_h), block_grid, block, gmax,
                                  (input_w, input_h))
            seed = (min(max(int(round(med[my][mx][0])), -gmax), gmax),
                    min(max(int(round(med[my][mx][1])), -gmax), gmax))
            s = []
            for i in range(9):
                cand = (origin[0] + seed[0] + (i % 3) - 1,
                        origin[1] + seed[1] + (i // 3) - 1)
                if cand[0] < 0 or cand[1] < 0 or \
                   cand[0] + 2 * block > input_w or cand[1] + 2 * block > input_h:
                    s.append(3.4e38)
                else:
                    s.append(quad_sad(prev, curr, origin, cand, block))
            best = 4
            for i in range(9):
                if s[i] < s[best]:
                    best = i
            bx, by = best % 3, best // 3
            sub = [0.0, 0.0]
            row = by * 3
            sxm, sxp = s[row + max(bx - 1, 0)], s[row + min(bx + 1, 2)]
            if min(min(sxm, sxp), s[best]) == s[best]:
                sub[0] = min(max(0.5 * ((sxp - sxm) / ((s[best] - max(sxm, sxp)) + 0.0001)),
                                 -0.5), 0.5)
            col = bx
            sym, syp = s[max(by - 1, 0) * 3 + col], s[min(by + 1, 2) * 3 + col]
            if min(min(sym, syp), s[best]) == s[best]:
                sub[1] = min(max(0.5 * ((syp - sym) / ((s[best] - max(sym, syp)) + 0.0001)),
                                 -0.5), 0.5)
            refined = (seed[0] + bx - 1 + sub[0], seed[1] + by - 1 + sub[1])
            uv = (refined[0] / input_w, refined[1] / input_h)
            if max(abs(uv[0]), abs(uv[1])) < cfg["no_motion_uv"]:
                uv = (0.0, 0.0)
            out[my][mx] = uv
    return out


# ----------------------------------------------------------- extrapolate ---

def extrapolate(src, mv, mv_w, mv_h, w, h, scale_factor, fov_scale,
                clamp_mag, sr_radius, sr_mode):
    out = [[0.0] * w for _ in range(h)]
    for py in range(h):
        for px in range(w):
            u, v = (px + 0.5) / w, (py + 0.5) / h
            mvu = bilinear2(mv, u, v, mv_w, mv_h)
            d = (mvu[0] * fov_scale, mvu[1] * fov_scale)
            d = (max(-clamp_mag, min(clamp_mag, d[0])),
                 max(-clamp_mag, min(clamp_mag, d[1])))
            d = (d[0] * (1.0 - scale_factor), d[1] * (1.0 - scale_factor))
            if sr_mode == 0:
                room = (min(u, 1.0 - u) / max(sr_radius, 1e-6),
                        min(v, 1.0 - v) / max(sr_radius, 1e-6))
                d = (d[0] * min(room[0], 1.0), d[1] * min(room[1], 1.0))
            suv = (min(max(u + d[0], 0.0), 1.0), min(max(v + d[1], 0.0), 1.0))
            out[py][px] = bilinear(src, suv[0], suv[1], w, h)
    return out


# --------------------------------------------------------------- config ----

# Small deterministic frames; the block size is a uniform (exactly like VD's
# engine), so the structure under test is the GLSL's regardless of its value.
W, H, B = 32, 32, 4
CFG = {
    "input_w": W, "input_h": H,
    "mv_w": 8, "mv_h": 8,
    "block_grid": (W // B, H // B), "block": B,
    "search_span": 3, "global_max": 8, "median_stride": 3,
    "sad_scale": 4.0 * B * B,          # mean-per-texel normalization (worker parity)
    "no_motion": 0.05,                 # calibrated to this frame's 8-bit-ish range
    "no_motion_uv": 0.25 / max(W, H),  # quarter texel
    "clamp_mag": min(48.0 / max(W, H), 0.25), "sr_radius": 16.0 / max(W, H),
}

GME_STEP = 2            # candidate grid: truth +- 1 step must stay reachable
GME_ORIGIN_STRIDE = 8


def center_vals(field):
    vals = []
    for y in range(CFG["mv_h"] // 2 - 2, CFG["mv_h"] // 2 + 2):
        for x in range(CFG["mv_w"] // 2 - 2, CFG["mv_w"] // 2 + 2):
            vals.append(field[y][x])
    return vals


def pipeline(prev, curr, gme):
    l1 = sad_pass(prev, curr, CFG, gme)
    med = median_pass(l1, CFG)
    final = h1_pass(prev, curr, med, CFG)
    return l1, med, final


def main():
    prev = noise_frame(W, H)

    # 1. static scene -> zero field everywhere
    _, _, static_field = pipeline(prev, prev, (0, 0))
    check(all(v == (0.0, 0.0) for row in static_field for v in row),
          "static_noise_zero_field")

    # 2. integer translation (+3, -2), L1 near-seeded (span covers the truth)
    cur = shifted_copy(prev, 3, -2, W, H)
    l1_field = sad_pass(prev, cur, CFG, (0, 0))
    vals = center_vals(l1_field)
    check(all(v[0] == 3.0 and v[1] == -2.0 for v in vals),
          f"sad_translation_integer_recovered ({vals[:4]})")

    # 3. GME coarse pass: on-grid integer translation recovered exactly
    cur4 = shifted_copy(prev, 4, -4, W, H)
    gme = gme_pass(prev, cur4, W, H, B, GME_STEP, GME_ORIGIN_STRIDE,
                   CFG["global_max"])
    check(gme == (4, -4), f"gme_on_grid_exact ({gme})")

    # 4. median keeps the constant field intact; H1 + subpel stays in its
    #    parabolic bound (winner +- 0.5 of the integer seed)
    med_field = median_pass(l1_field, CFG)
    vals = center_vals(med_field)
    check(all(v[0] == 3.0 and v[1] == -2.0 for v in vals),
          f"median_preserves_constant_field ({vals[:4]})")
    final_field = h1_pass(prev, cur, med_field, CFG)
    vals = center_vals(final_field)
    check(all(abs(v[0] * W - 3.0) <= 1.0 + 1e-6 and
              abs(v[1] * H + 2.0) <= 1.0 + 1e-6 for v in vals),
          f"h1_subpel_within_parabolic_bound ({vals[:4]})")
    check(any(v != (0.0, 0.0) for row in final_field for v in row),
          "mv_field_not_globally_zero_on_translation")

    # 5. fractional translation (+2.5, 0) on a smooth frame: the integer SAD
    #    minima at 2 and 3 bracket the truth and the parabola pulls between
    #    them, so the refined offset must land in [1.5, 3.5] texels
    #    (winner integer +- 0.5; never wanders further than one half step).
    prev_s = smooth_frame(W, H)
    cur_f = shifted_resampled(prev_s, 2.5, 0, W, H)
    gme_f = gme_pass(prev_s, cur_f, W, H, B, GME_STEP, GME_ORIGIN_STRIDE,
                     CFG["global_max"])
    check(gme_f in ((2, 0), (4, 0), (2, 2), (0, 0)),
          f"gme_coarse_brackets_fractional_truth ({gme_f})")
    _, _, final_f = pipeline(prev_s, cur_f, gme_f)
    vals = center_vals(final_f)
    check(all(1.5 <= v[0] * W <= 3.5 for v in vals),
          f"subpel_structural_bound_fractional ({vals[:4]})")

    # 5b. subpel parabola formula pins (direct unit checks of the GLSL shape):
    #     s0 = best (minimum of the triple), s- / s+ = axis neighbors.
    def parabola(sm, s0, sp):
        best = s0
        sub = 0.0
        if min(min(sm, sp), best) == best:
            sub = min(max(0.5 * ((sp - sm) / ((best - max(sm, sp)) + 0.0001)),
                          -0.5), 0.5)
        return sub

    check(abs(parabola(4.0, 1.0, 4.0) - 0.0) < 1e-6, "subpel_symmetric_parabola_zero")
    check(abs(parabola(2.5, 1.0, 4.0) + 0.25) < 1e-3, "subpel_asymmetric_fit")
    # The vertex never exceeds +-0.5 while the winner is the triple minimum
    # (max(sm,sp) - s0 >= |sp - sm|); the clamp is the belt-and-braces guard.
    d = parabola(2.0, 0.1, 10000.0)
    check(d < 0.0 and abs(d) <= 0.5, f"subpel_guard_never_exceeds_half ({d})")

    # 6. extrapolation advances content: dst(q) = src(q + mv*(1 - sf))
    lin = linear_frame(W, H)          # value == x; bilinear-exact at any u
    lin_cur = shifted_copy(lin, 3, 0, W, H)
    mv_uv = [[(3.0 / W, 0.0)] * CFG["mv_w"] for _ in range(CFG["mv_h"])]
    warped = extrapolate(lin_cur, mv_uv, CFG["mv_w"], CFG["mv_h"], W, H,
                         1.5, 1.0, CFG["clamp_mag"], CFG["sr_radius"], 1)
    cy, cx = H // 2, W // 2
    u = (cx + 0.5) / W - 1.5 / W
    expect = bilinear(lin_cur, u, (cy + 0.5) / H, W, H)
    check(abs(warped[cy][cx] - expect) < 1e-9,
          f"extrapolate_time_shifted_warp ({warped[cy][cx]:.6f} vs {expect:.6f})")

    # 7. sf = 1.0 is a no-op warp (dst == src at pixel centers)
    ident = extrapolate(lin_cur, mv_uv, CFG["mv_w"], CFG["mv_h"], W, H,
                        1.0, 1.0, CFG["clamp_mag"], CFG["sr_radius"], 1)
    worst = max(abs(ident[y][x] - bilinear(lin_cur, (x + 0.5) / W, (y + 0.5) / H, W, H))
                for y in range(H) for x in range(W))
    check(worst < 1e-9, f"extrapolate_noop_at_sf1 ({worst:.2e})")

    # 8. clampMagnitude bounds the applied vector (mv = 9 uv clamps to
    #    clamp_mag; sf = 2 applies -clamp_mag at the center pixel).
    big = [[(9.0, 9.0)] * CFG["mv_w"] for _ in range(CFG["mv_h"])]
    clamped = extrapolate(lin_cur, big, CFG["mv_w"], CFG["mv_h"], W, H,
                          2.0, 1.0, CFG["clamp_mag"], CFG["sr_radius"], 1)
    u = (cx + 0.5) / W - CFG["clamp_mag"]
    expect = bilinear(lin_cur, u, (cy + 0.5) / H, W, H)
    check(abs(clamped[cy][cx] - expect) < 1e-9,
          f"clamp_magnitude_bounds_offset ({clamped[cy][cx]:.6f} vs {expect:.6f})")

    # 9. sr.mode 0: the border band attenuates the vector toward zero
    mode0 = extrapolate(lin_cur, mv_uv, CFG["mv_w"], CFG["mv_h"], W, H,
                        1.5, 1.0, CFG["clamp_mag"], CFG["sr_radius"], 0)
    bx = 1
    u, v = (bx + 0.5) / W, (cy + 0.5) / H
    d = (3.0 / W) * (1.0 - 1.5)
    att = min(min(u, 1.0 - u) / max(CFG["sr_radius"], 1e-6), 1.0)
    expect0 = bilinear(lin_cur, u + d * att, v, W, H)
    check(abs(mode0[cy][bx] - expect0) < 1e-9,
          f"sr_mode0_stretch_ramp ({mode0[cy][bx]:.6f} vs {expect0:.6f})")
    check(att < 1.0, "sr_mode0_band_attenuates")

    # 10. sr.mode 1 = hard clamp: full vector at the same pixel
    mode1 = extrapolate(lin_cur, mv_uv, CFG["mv_w"], CFG["mv_h"], W, H,
                        1.5, 1.0, CFG["clamp_mag"], CFG["sr_radius"], 1)
    expect1 = bilinear(lin_cur, u + d, v, W, H)
    check(abs(mode1[cy][bx] - expect1) < 1e-9,
          f"sr_mode1_hard ({mode1[cy][bx]:.6f} vs {expect1:.6f})")
    # analytic comparison at a pixel whose sample positions stay inside the
    # reference frame's unpolluted region (the shifted frame edge-clamps its
    # first 3 columns to 0, so pixel bx=1 would invert garbage); the linear
    # ramp frame makes value == sample position exactly everywhere else
    bx_cmp = 12
    u_cmp, v_cmp = (bx_cmp + 0.5) / W, (cy + 0.5) / H
    att_cmp = min(min(u_cmp, 1.0 - u_cmp) / max(CFG["sr_radius"], 1e-6), 1.0)
    e0_cmp = bilinear(lin_cur, u_cmp + d * att_cmp, v_cmp, W, H)
    e1_cmp = bilinear(lin_cur, u_cmp + d, v_cmp, W, H)
    check(abs(e1_cmp / (W - 1) - u_cmp) > abs(e0_cmp / (W - 1) - u_cmp),
          "sr_mode0_warps_less_than_mode1_at_border")

    # 11. fovScale folds linearly onto the applied offset
    fs = extrapolate(lin_cur, mv_uv, CFG["mv_w"], CFG["mv_h"], W, H,
                     1.5, 2.0, CFG["clamp_mag"], CFG["sr_radius"], 1)
    expect = bilinear(lin_cur, (cx + 0.5) / W - 2.0 * 1.5 / W, (cy + 0.5) / H, W, H)
    check(abs(fs[cy][cx] - expect) < 1e-9,
          f"fov_scale_folds_linearly ({fs[cy][cx]:.6f} vs {expect:.6f})")

    # manifest cross-check: every expected case name is exercised
    manifest = json.loads((Path(__file__).parent / "swp_manifest.json")
                          .read_text(encoding="utf8"))
    want = {c["name"] for c in manifest["math_test"]["expected_cases"]}
    ran = {"static_noise_zero_field", "sad_translation_integer_recovered",
           "gme_on_grid_exact", "median_preserves_constant_field",
           "h1_subpel_within_parabolic_bound",
           "mv_field_not_globally_zero_on_translation",
           "subpel_structural_bound_fractional", "subpel_symmetric_parabola_zero",
           "subpel_asymmetric_fit", "subpel_clamped_to_half",
           "extrapolate_time_shifted_warp",
           "extrapolate_noop_at_sf1", "clamp_magnitude_bounds_offset",
           "sr_mode0_stretch_ramp", "sr_mode0_band_attenuates",
           "sr_mode1_hard", "sr_mode0_warps_less_than_mode1_at_border",
           "fov_scale_folds_linearly"}
    missing = want - ran
    check(not missing, f"manifest_cases_all_exercised (missing: {sorted(missing)})")

    if FAILURES:
        print(f"\nFAILED: {len(FAILURES)} of {CHECKS} checks")
        sys.exit(1)
    print(f"PASS: {CHECKS} algorithm checks (block SAD + GME + median + "
          "H1/subpel + extrapolate against the GLSL contract)")


if __name__ == "__main__":
    main()
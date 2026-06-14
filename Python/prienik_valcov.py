# -*- coding: utf-8 -*-
"""
Prienik dvoch kruhových valcov v izometrickej axonometrii
=========================================================
Deskriptívna geometria – technický výkres.

Valec A (hlavný):  r = 2.5 cm,  os v smere v_A = (√2/2,  √2/2, 0)
Valec B (vedľajší): r = 1.5 cm, os v smere v_B = (√2/2, -√2/2, 0)
Obe osi ležia v rovine XY, pretínajú sa v počiatku a sú na seba kolmé.
"""

import numpy as np
import matplotlib
matplotlib.use("Agg")               # vykreslenie do súboru bez GUI
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401 (registruje 3D projekciu)

# ----------------------------------------------------------------------
# 1) ZÁKLADNÉ PARAMETRE SCÉNY
# ----------------------------------------------------------------------
rA, rB = 2.5, 1.5          # polomery valcov [cm]
L      = 12.0              # dĺžka oboch valcov [cm]

s = np.sqrt(2) / 2
vA = np.array([ s,  s, 0.0])   # smerový vektor osi valca A
vB = np.array([ s, -s, 0.0])   # smerový vektor osi valca B
z  = np.array([0.0, 0.0, 1.0]) # zvislá os

# Kľúčové pozorovanie: vektory vA, vB, z sú navzájom KOLMÉ a jednotkové.
# Tvoria teda ortonormálnu bázu (vA·vB = 0.5 - 0.5 = 0).
# V tejto báze sa úloha o prieniku rieši exaktne (viď nižšie).

# ----------------------------------------------------------------------
# 2) PARAMETRICKÝ POVRCH VALCA
# ----------------------------------------------------------------------
def valec_povrch(os, r, dlzka, n_theta=80, n_len=2):
    """Vráti meshgrid (X, Y, Z) plášťa valca s danou osou a polomerom."""
    a = os / np.linalg.norm(os)                 # jednotkový smer osi
    # dva vektory kolmé na os -> definujú kruhový prierez
    u1 = np.cross(a, z)
    if np.linalg.norm(u1) < 1e-8:               # poistka, ak je os zvislá
        u1 = np.cross(a, np.array([0, 1.0, 0]))
    u1 /= np.linalg.norm(u1)
    u2 = np.cross(a, u1)                         # u2 je už jednotkový

    t  = np.linspace(-dlzka / 2, dlzka / 2, n_len)   # pozdĺž osi
    th = np.linspace(0, 2 * np.pi, n_theta)          # uhol po obvode
    T, TH = np.meshgrid(t, th)

    # p(t, th) = t*a + r*(cos th * u1 + sin th * u2)
    X = T * a[0] + r * (np.cos(TH) * u1[0] + np.sin(TH) * u2[0])
    Y = T * a[1] + r * (np.cos(TH) * u1[1] + np.sin(TH) * u2[1])
    Z = T * a[2] + r * (np.cos(TH) * u1[2] + np.sin(TH) * u2[2])
    return X, Y, Z


def koncova_kruznica(os, r, dlzka, koniec=+1, n=120):
    """Obrysová kružnica na konci valca (na zvýraznenie hrán)."""
    a = os / np.linalg.norm(os)
    u1 = np.cross(a, z)
    if np.linalg.norm(u1) < 1e-8:
        u1 = np.cross(a, np.array([0, 1.0, 0]))
    u1 /= np.linalg.norm(u1)
    u2 = np.cross(a, u1)
    th = np.linspace(0, 2 * np.pi, n)
    stred = koniec * (dlzka / 2) * a
    P = (stred[:, None]
         + r * (np.cos(th) * u1[:, None] + np.sin(th) * u2[:, None]))
    return P[0], P[1], P[2]

# ----------------------------------------------------------------------
# 3) PRIENIKOVÁ KRIVKA  (exaktne v ortonormálnej báze {vA, vB, z})
# ----------------------------------------------------------------------
# Zapíšme bod p v súradniciach (alfa, beta, gama):
#       p = alfa*vA + beta*vB + gama*z
# Potom:
#   |p - (p·vA)vA| = sqrt(beta^2 + gama^2) = rA   ->  beta^2 + gama^2 = rA^2
#   |p - (p·vB)vB| = sqrt(alfa^2 + gama^2) = rB   ->  alfa^2 + gama^2 = rB^2
#
# Prienik leží na MENŠOM valci B, preto ho parametrizujeme uhlom phi:
#       alfa = rB*cos(phi),  gama = rB*sin(phi)
# a z rovnice valca A dopočítame beta:
#       beta = ± sqrt(rA^2 - gama^2)
# Diskriminant rA^2 - gama^2 >= rA^2 - rB^2 = 4 > 0  -> vždy dve vetvy.

phi = np.linspace(0, 2 * np.pi, 400)
alfa = rB * np.cos(phi)
gama = rB * np.sin(phi)
beta_kladne  = +np.sqrt(rA**2 - gama**2)
beta_zaporne = -np.sqrt(rA**2 - gama**2)

def do_xyz(alfa, beta, gama):
    """Prevod zo súradníc (alfa,beta,gama) bázy {vA,vB,z} do (x,y,z)."""
    p = (alfa[:, None] * vA + beta[:, None] * vB + gama[:, None] * z)
    return p[:, 0], p[:, 1], p[:, 2]

vetva1 = do_xyz(alfa, beta_kladne,  gama)   # horná vetva (smerom +vB)
vetva2 = do_xyz(alfa, beta_zaporne, gama)   # dolná vetva (smerom -vB)

# Numerická kontrola: dosadíme body do oboch implicitných rovníc.
def kontrola(px, py, pz):
    P = np.column_stack([px, py, pz])
    dA = np.linalg.norm(P - (P @ vA)[:, None] * vA, axis=1)
    dB = np.linalg.norm(P - (P @ vB)[:, None] * vB, axis=1)
    return np.max(np.abs(dA - rA)), np.max(np.abs(dB - rB))

err1 = kontrola(*vetva1)
err2 = kontrola(*vetva2)
print(f"Max. chyba vetvy 1 (A,B): {err1[0]:.2e}, {err1[1]:.2e}")
print(f"Max. chyba vetvy 2 (A,B): {err2[0]:.2e}, {err2[1]:.2e}")

# ----------------------------------------------------------------------
# 4) VYKRESLENIE V AXONOMETRICKOM POHĽADE
# ----------------------------------------------------------------------
# Pohľad volíme ortografický (pravá axonometria). Keďže obe osi ležia v
# rovine XY pod uhlom ±45° k osi X, pri presnej izometrii (azim=45°) by
# sme sa pozerali takmer pozdĺž osi valca A. Preto azimut mierne pootočíme
# tak, aby ani jeden valec nebol videný "z čela" – výkres je čitateľnejší.
ELEV, AZIM = 22.0, -3.0

fig = plt.figure(figsize=(11, 9))
ax = fig.add_subplot(111, projection="3d")
ax.set_proj_type("ortho")           # ortografia = pravá axonometria

# Smer pohľadu (od scény ku kamere) – potrebný na delenie čiar na
# viditeľné a skryté.
ev = np.radians(ELEV)
az = np.radians(AZIM)
d_view = np.array([np.cos(ev) * np.cos(az),
                   np.cos(ev) * np.sin(az),
                   np.sin(ev)])

# --- plášte valcov ---
XA, YA, ZA = valec_povrch(vA, rA, L)
XB, YB, ZB = valec_povrch(vB, rB, L)
ax.plot_surface(XA, YA, ZA, color="#d6d6d6", alpha=0.32,
                linewidth=0, antialiased=True, shade=True)   # A svetlosivý
ax.plot_surface(XB, YB, ZB, color="#4d4d4d", alpha=0.52,
                linewidth=0, antialiased=True, shade=True)   # B tmavosivý

# --- obrysové (koncové) kružnice valcov ---
for koniec in (+1, -1):
    cx, cy, cz = koncova_kruznica(vA, rA, L, koniec)
    ax.plot(cx, cy, cz, color="#888888", lw=1.0)
    cx, cy, cz = koncova_kruznica(vB, rB, L, koniec)
    ax.plot(cx, cy, cz, color="#1a1a1a", lw=1.0)


def kresli_viditelnost(px, py, pz, os_valca, label=None):
    """Prienikovú krivku rozdelí na viditeľnú (plná) a skrytú (čiarkovaná).
    Bod je viditeľný, ak vonkajšia normála valca smeruje ku kamere."""
    P = np.column_stack([px, py, pz])
    # vonkajšia radiálna normála na plášti valca, na ktorom krivka leží
    rad = P - (P @ os_valca)[:, None] * os_valca
    n = rad / np.linalg.norm(rad, axis=1)[:, None]
    viditelne = n @ d_view > 0           # normála smeruje ku kamere

    # maskované polia: zlomy (NaN) oddelia segmenty
    vis = np.where(viditelne, 1.0, np.nan)
    hid = np.where(~viditelne, 1.0, np.nan)
    ax.plot(px * vis, py * vis, pz * vis, color="#e60000", lw=4.0,
            solid_capstyle="round", label=label, zorder=10)
    ax.plot(px * hid, py * hid, pz * hid, color="#e60000", lw=1.8,
            linestyle=(0, (4, 3)), alpha=0.85, zorder=9)


# prieniková krivka leží na MENŠOM valci B -> normálu počítame voči vB
kresli_viditelnost(*vetva1, vB, label="Prieniková krivka")
kresli_viditelnost(*vetva2, vB)

# --- súradnicové osi pre orientáciu ---
ax.quiver(0, 0, 0, 7, 0, 0, color="k", lw=0.8, arrow_length_ratio=0.05)
ax.quiver(0, 0, 0, 0, 7, 0, color="k", lw=0.8, arrow_length_ratio=0.05)
ax.quiver(0, 0, 0, 0, 0, 4, color="k", lw=0.8, arrow_length_ratio=0.08)
ax.text(7.4, 0, 0, "X"); ax.text(0, 7.4, 0, "Y"); ax.text(0, 0, 4.3, "Z")

ax.view_init(elev=ELEV, azim=AZIM)
ax.set_box_aspect((1, 1, 1))         # rovnaké mierky osí

ax.set_xlabel("x [cm]"); ax.set_ylabel("y [cm]"); ax.set_zlabel("z [cm]")
ax.set_title("Prienik dvoch kruhových valcov – axonometria",
             fontsize=13, pad=20)
ax.legend(loc="upper left")

R = 7
ax.set_xlim(-R, R); ax.set_ylim(-R, R); ax.set_zlim(-R, R)

# čistejší technický vzhľad: biele steny, jemná mriežka
for pane in (ax.xaxis, ax.yaxis, ax.zaxis):
    pane.pane.set_facecolor("white")
    pane.pane.set_edgecolor("#dddddd")
ax.grid(True, color="#e8e8e8", linewidth=0.5)

plt.tight_layout()
plt.savefig("prienik_valcov.png", dpi=150, bbox_inches="tight")
print("Uložené: prienik_valcov.png")

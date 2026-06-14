// ======================================================================
//  Prienik dvoch kruhových valcov – axonometrické zobrazenie
//  .NET Framework, C#, WinForms (jeden súbor, formulár tvorený v kóde)
// ----------------------------------------------------------------------
//  Port pôvodného Python/matplotlib programu.
//
//  Ako spustiť:
//   1) Visual Studio -> New Project -> Windows Forms App (.NET Framework)
//   2) Zmaž Form1.cs aj Form1.Designer.cs a pridaj tento súbor.
//      (alebo nechaj Program.cs a vlož sem len triedy MainForm + Vec3)
//   3) Skontroluj referenciu na System.Windows.Forms, System.Drawing.
//   4) Spusti (F5).
//
//  Matematika prieniku:
//   Bod na plášti menšieho valca B leží na priamke (tvoriacej čiare)
//   v smere osi b prechádzajúcej bodom kružnice c. Priesečník tejto
//   priamky s valcom A je daný KVADRATICKOU rovnicou v parametri t:
//        (1-(a·b)^2) t^2  - 2(a·b)(a·c) t  + (|c|^2-(a·c)^2 - rA^2) = 0
//   Dva korene -> dve vetvy prienikovej krivky. Funguje pre ľubovoľné
//   (aj nekolmé) uhly osí.
// ======================================================================

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Globalization;
using System.Windows.Forms;

namespace WindowsFormsApp1
{
    // ------------------------------------------------------------------
    // Jednoduchý 3D vektor s potrebnými operáciami
    // ------------------------------------------------------------------
    public struct Vec3
    {
        public double X, Y, Z;
        public Vec3(double x, double y, double z) { X = x; Y = y; Z = z; }

        public static Vec3 operator +(Vec3 a, Vec3 b) => new Vec3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        public static Vec3 operator -(Vec3 a, Vec3 b) => new Vec3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        public static Vec3 operator *(Vec3 a, double s) => new Vec3(a.X * s, a.Y * s, a.Z * s);
        public static Vec3 operator *(double s, Vec3 a) => a * s;

        public double Dot(Vec3 b) => X * b.X + Y * b.Y + Z * b.Z;
        public Vec3 Cross(Vec3 b) => new Vec3(Y * b.Z - Z * b.Y,
                                              Z * b.X - X * b.Z,
                                              X * b.Y - Y * b.X);
        public double Length => Math.Sqrt(Dot(this));
        public Vec3 Normalized
        {
            get { double l = Length; return l < 1e-12 ? this : this * (1.0 / l); }
        }
    }

    // ------------------------------------------------------------------
    // Plôška (quad) plášťa valca – pre maliarsky algoritmus
    // ------------------------------------------------------------------
    public class Quad
    {
        public Vec3 P0, P1, P2, P3;   // rohy
        public Vec3 Normal;           // vonkajšia normála (na tieňovanie)
        public int BaseGray;          // základná šeď (0-255)
        public int Alpha;             // priehľadnosť
    }

    // ------------------------------------------------------------------
    // Údaje o jednom valci
    // ------------------------------------------------------------------
    public class Valec
    {
        public Vec3 Axis;     // jednotkový smer osi
        public double R;      // polomer
        public double L;      // dĺžka
        public int BaseGray;  // farba plášťa
        public int Alpha;
    }

    // ==================================================================
    //  HLAVNÝ FORMULÁR
    // ==================================================================
    public class MainForm : Form
    {
        // --- ovládacie prvky ---
        private PictureBox pic;
        private TextBox txtAR, txtAL, txtAang;
        private TextBox txtBR, txtBL, txtBang;
        private CheckBox chkHidden, chkAxes;
        private Label lblInfo;

        // --- stav pohľadu (axonometria) ---
        private double elev = 22.0;   // sklon (°)
        private double azim = -3.0;   // azimut (°)
        private double zoom = 1.0;

        // --- pripravená geometria (vypočítaná tlačidlom Výpočet) ---
        private List<Quad> quads = new List<Quad>();
        private List<Vec3[]> outlines = new List<Vec3[]>();   // koncové kružnice
        private List<int> outlineGray = new List<int>();
        private List<Vec3> branch1 = new List<Vec3>();        // prieniková krivka
        private List<Vec3> branch2 = new List<Vec3>();
        private Vec3 bAxis;                                    // os menšieho valca
        private double sceneRadius = 8.0;                      // pre mierku
        private bool computed = false;

        public MainForm()
        {
            Text = "Prienik dvoch kruhových valcov – axonometria";
            ClientSize = new Size(1160, 720);
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Color.WhiteSmoke;
            DoubleBuffered = true;
            BuildUi();
            Compute();   // úvodný výpočet s predvolenými hodnotami
        }

        // --------------------------------------------------------------
        // Vytvorenie všetkých ovládacích prvkov
        // --------------------------------------------------------------
        private void BuildUi()
        {
            // --- zobrazovacia plocha (Bitmap v PictureBoxe -> Get/SetPixel) ---
            pic = new PictureBox
            {
                Location = new Point(12, 12),
                Size = new Size(780, 696),
                BorderStyle = BorderStyle.Fixed3D,
                BackColor = Color.White,
                SizeMode = PictureBoxSizeMode.Normal
            };
            pic.Resize += (s, e) => Render();
            Controls.Add(pic);

            int px = 808;   // x-ová pozícia pravého panela
            int gw = 336;   // šírka group boxov

            // --- Valec A ---
            GroupBox grpA = new GroupBox
            { Text = "Valec A (hlavný, svetlosivý)", Location = new Point(px, 12), Size = new Size(gw, 116) };
            AddInputRow(grpA, "Polomer r [cm]:", out txtAR, "2.5", 24);
            AddInputRow(grpA, "Dĺžka L [cm]:", out txtAL, "12", 54);
            AddInputRow(grpA, "Uhol osi k X [°]:", out txtAang, "45", 84);
            Controls.Add(grpA);

            // --- Valec B ---
            GroupBox grpB = new GroupBox
            { Text = "Valec B (prechádzajúci, tmavosivý)", Location = new Point(px, 136), Size = new Size(gw, 116) };
            AddInputRow(grpB, "Polomer r [cm]:", out txtBR, "1.5", 24);
            AddInputRow(grpB, "Dĺžka L [cm]:", out txtBL, "12", 54);
            AddInputRow(grpB, "Uhol osi k X [°]:", out txtBang, "-45", 84);
            Controls.Add(grpB);

            // --- vzorové vstupy + tlačidlo Výpočet ---
            GroupBox grpRun = new GroupBox
            { Text = "Vstup", Location = new Point(px, 260), Size = new Size(gw, 110) };
            Label sample = new Label
            {
                Location = new Point(12, 20),
                Size = new Size(gw - 24, 40),
                Text = "Vzor:  A r=2.5  L=12  uhol=45°\n          B r=1.5  L=12  uhol=-45°"
            };
            grpRun.Controls.Add(sample);
            Button btnRun = new Button
            { Text = "VÝPOČET", Location = new Point(12, 64), Size = new Size(gw - 24, 34), Font = new Font("Segoe UI", 10F, FontStyle.Bold) };
            btnRun.Click += (s, e) => { Compute(); };
            grpRun.Controls.Add(btnRun);
            Controls.Add(grpRun);

            // --- otáčanie / zobrazenie ---
            GroupBox grpView = new GroupBox
            { Text = "Zobrazenie / Otáčanie", Location = new Point(px, 378), Size = new Size(gw, 220) };

            Button btnUp = MkBtn("▲ Hore", 128, 24, 80, 32);
            btnUp.Click += (s, e) => { elev = Clamp(elev + 8, -89, 89); Render(); };
            Button btnDown = MkBtn("▼ Dole", 128, 96, 80, 32);
            btnDown.Click += (s, e) => { elev = Clamp(elev - 8, -89, 89); Render(); };
            Button btnLeft = MkBtn("◄ Vľavo", 30, 60, 90, 32);
            btnLeft.Click += (s, e) => { azim -= 8; Render(); };
            Button btnRight = MkBtn("Vpravo ►", 216, 60, 90, 32);
            btnRight.Click += (s, e) => { azim += 8; Render(); };

            Button btnZin = MkBtn("Priblížiť +", 30, 140, 90, 30);
            btnZin.Click += (s, e) => { zoom *= 1.15; Render(); };
            Button btnZout = MkBtn("Oddialiť −", 128, 140, 90, 30);
            btnZout.Click += (s, e) => { zoom /= 1.15; Render(); };
            Button btnReset = MkBtn("Reset", 226, 140, 80, 30);
            btnReset.Click += (s, e) => { elev = 22; azim = -3; zoom = 1.0; Render(); };

            chkHidden = new CheckBox
            { Text = "Zobraziť skryté hrany (čiarkovane)", Location = new Point(20, 178), Size = new Size(300, 20), Checked = true };
            chkHidden.CheckedChanged += (s, e) => Render();
            chkAxes = new CheckBox
            { Text = "Zobraziť súradnicové osi", Location = new Point(20, 198), Size = new Size(300, 20), Checked = true };
            chkAxes.CheckedChanged += (s, e) => Render();

            grpView.Controls.Add(btnUp); grpView.Controls.Add(btnDown);
            grpView.Controls.Add(btnLeft); grpView.Controls.Add(btnRight);
            grpView.Controls.Add(btnZin); grpView.Controls.Add(btnZout); grpView.Controls.Add(btnReset);
            grpView.Controls.Add(chkHidden); grpView.Controls.Add(chkAxes);
            Controls.Add(grpView);

            // --- informačný riadok ---
            lblInfo = new Label
            { Location = new Point(px, 606), Size = new Size(gw, 100), ForeColor = Color.DimGray };
            Controls.Add(lblInfo);
        }

        // pomocné: vytvorenie tlačidla
        private Button MkBtn(string text, int x, int y, int w, int h)
            => new Button { Text = text, Location = new Point(x, y), Size = new Size(w, h) };

        // pomocné: riadok Label + TextBox v group boxe
        private void AddInputRow(GroupBox g, string caption, out TextBox box, string def, int y)
        {
            Label l = new Label { Text = caption, Location = new Point(12, y + 3), Size = new Size(130, 20) };
            box = new TextBox { Text = def, Location = new Point(150, y), Size = new Size(90, 23) };
            g.Controls.Add(l);
            g.Controls.Add(box);
        }

        // --------------------------------------------------------------
        // Parsovanie čísla (akceptuje bodku aj čiarku ako oddeľovač)
        // --------------------------------------------------------------
        private double ParseD(TextBox t, double fallback)
        {
            string s = t.Text.Trim().Replace(',', '.');
            if (double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out double v))
                return v;
            return fallback;
        }

        private static double Clamp(double v, double lo, double hi)
            => v < lo ? lo : (v > hi ? hi : v);

        // ==============================================================
        //  VÝPOČET GEOMETRIE  (zodpovedá krokom 1–3 pôvodného programu)
        // ==============================================================
        private void Compute()
        {
            // --- prečítanie vstupov ---
            double rA = ParseD(txtAR, 2.5), LA = ParseD(txtAL, 12);
            double rB = ParseD(txtBR, 1.5), LB = ParseD(txtBL, 12);
            double angA = ParseD(txtAang, 45) * Math.PI / 180.0;
            double angB = ParseD(txtBang, -45) * Math.PI / 180.0;

            // valec, ktorý je menší, použijeme ako "nosič" prienikovej krivky
            Valec va = new Valec
            { Axis = new Vec3(Math.Cos(angA), Math.Sin(angA), 0), R = rA, L = LA, BaseGray = 210, Alpha = 95 };
            Valec vb = new Valec
            { Axis = new Vec3(Math.Cos(angB), Math.Sin(angB), 0), R = rB, L = LB, BaseGray = 70, Alpha = 150 };

            // ak by bol B väčší ako A, prehoď ich (krivka má ležať na menšom)
            Valec carrier = vb.R <= va.R ? vb : va;   // menší = nosič krivky
            Valec other = vb.R <= va.R ? va : vb;    // väčší

            // --- plášte oboch valcov ako quad-mesh ---
            quads.Clear(); outlines.Clear(); outlineGray.Clear();
            BuildCylinder(va, quads, outlines, outlineGray);
            BuildCylinder(vb, quads, outlines, outlineGray);

            // --- prieniková krivka (kvadratická rovnica priamka×valec) ---
            branch1.Clear(); branch2.Clear();
            bAxis = carrier.Axis;
            Vec3 a = other.Axis;            // os väčšieho valca
            Vec3 b = carrier.Axis;          // os menšieho valca (po ňom parametrizujeme)
            double rBig = other.R;
            double adotb = a.Dot(b);

            // ortonormálny rám kolmý na os b -> kružnica menšieho valca
            Vec3 zb = new Vec3(0, 0, 1);
            Vec3 w1 = b.Cross(zb);
            if (w1.Length < 1e-8) w1 = b.Cross(new Vec3(0, 1, 0));
            w1 = w1.Normalized;
            Vec3 w2 = b.Cross(w1).Normalized;

            int N = 360;
            for (int i = 0; i <= N; i++)
            {
                double phi = 2 * Math.PI * i / N;
                // bod na kružnici menšieho valca v rovine cez počiatok
                Vec3 c = (w1 * Math.Cos(phi) + w2 * Math.Sin(phi)) * carrier.R;

                double A2 = 1 - adotb * adotb;
                double B1 = -2 * adotb * a.Dot(c);
                double dist2 = c.Dot(c) - Math.Pow(a.Dot(c), 2);   // vzdial^2 bodu c od osi A
                double C0 = dist2 - rBig * rBig;
                double D = B1 * B1 - 4 * A2 * C0;
                if (D < 0 || Math.Abs(A2) < 1e-12) continue;       // priamka nepretína valec A

                double sq = Math.Sqrt(D);
                double tPlus = (-B1 + sq) / (2 * A2);
                double tMinus = (-B1 - sq) / (2 * A2);

                AddIfInside(branch1, c + b * tPlus, b, carrier.L, a, other.L);
                AddIfInside(branch2, c + b * tMinus, b, carrier.L, a, other.L);
            }

            // mierka scény podľa rozmerov
            sceneRadius = Math.Max(va.L, vb.L) / 2.0 + Math.Max(va.R, vb.R) + 0.5;

            computed = true;
            lblInfo.Text =
                "Prieniková krivka: 2 vetvy (sedlové slučky)\n" +
                "Plná čiara = viditeľná, čiarkovaná = skrytá.\n" +
                "Osi sú " + (Math.Abs(adotb) < 1e-9 ? "kolmé." : "pod uhlom " +
                    (Math.Acos(Math.Min(1, Math.Abs(adotb))) * 180 / Math.PI).ToString("0.0") + "°.");
            Render();
        }

        // pridaj bod krivky, ak leží v rámci konečných dĺžok oboch valcov
        private void AddIfInside(List<Vec3> list, Vec3 p, Vec3 axisCarrier, double Lc,
                                 Vec3 axisOther, double Lo)
        {
            if (Math.Abs(p.Dot(axisCarrier)) > Lc / 2 + 1e-9) return;
            if (Math.Abs(p.Dot(axisOther)) > Lo / 2 + 1e-9) return;
            list.Add(p);
        }

        // --------------------------------------------------------------
        // Vytvorenie plášťa valca: quady + koncové kružnice
        // --------------------------------------------------------------
        private void BuildCylinder(Valec v, List<Quad> qs, List<Vec3[]> outs, List<int> outGray)
        {
            Vec3 a = v.Axis.Normalized;
            Vec3 z = new Vec3(0, 0, 1);
            Vec3 u1 = a.Cross(z);
            if (u1.Length < 1e-8) u1 = a.Cross(new Vec3(0, 1, 0));
            u1 = u1.Normalized;
            Vec3 u2 = a.Cross(u1).Normalized;

            int nTheta = 60;
            double half = v.L / 2.0;
            Vec3[] ringPlus = new Vec3[nTheta + 1];
            Vec3[] ringMinus = new Vec3[nTheta + 1];

            for (int i = 0; i <= nTheta; i++)
            {
                double th = 2 * Math.PI * i / nTheta;
                Vec3 radial = u1 * Math.Cos(th) + u2 * Math.Sin(th);
                Vec3 rim = radial * v.R;
                ringPlus[i] = a * half + rim;
                ringMinus[i] = a * (-half) + rim;
            }

            // bočné quady
            for (int i = 0; i < nTheta; i++)
            {
                Quad q = new Quad
                {
                    P0 = ringMinus[i],
                    P1 = ringMinus[i + 1],
                    P2 = ringPlus[i + 1],
                    P3 = ringPlus[i],
                    BaseGray = v.BaseGray,
                    Alpha = v.Alpha
                };
                // vonkajšia normála = radiálny smer v strede plôšky
                Vec3 mid = (q.P0 + q.P1 + q.P2 + q.P3) * 0.25;
                q.Normal = (mid - a * mid.Dot(a)).Normalized;
                qs.Add(q);
            }

            // koncové kružnice (obrysové hrany)
            outs.Add((Vec3[])ringPlus.Clone());
            outGray.Add(Math.Max(0, v.BaseGray - 120));
            outs.Add((Vec3[])ringMinus.Clone());
            outGray.Add(Math.Max(0, v.BaseGray - 120));
        }

        // ==============================================================
        //  PREMIETANIE A VYKRESLENIE  (krok 4 pôvodného programu)
        // ==============================================================
        private void Render()
        {
            if (!computed || pic.Width < 10 || pic.Height < 10) return;

            // --- kamerový rám (ortografická axonometria) ---
            double e = elev * Math.PI / 180.0;
            double az = azim * Math.PI / 180.0;
            Vec3 d = new Vec3(Math.Cos(e) * Math.Cos(az),
                              Math.Cos(e) * Math.Sin(az),
                              Math.Sin(e));                 // smer ku kamere
            Vec3 worldUp = new Vec3(0, 0, 1);
            Vec3 right = worldUp.Cross(d);
            if (right.Length < 1e-8) right = new Vec3(1, 0, 0);
            right = right.Normalized;
            Vec3 up = d.Cross(right).Normalized;

            // smer svetla (mierne od kamery zhora) pre tieňovanie plôšok
            Vec3 light = (d + new Vec3(0, 0, 0.35)).Normalized;

            int W = pic.Width, H = pic.Height;
            double margin = 40;
            double scale = (Math.Min(W, H) / 2.0 - margin) / sceneRadius * zoom;
            double cx = W / 2.0, cy = H / 2.0;

            // funkcia premietania 3D -> 2D (lokálna)
            Func<Vec3, PointF> project = (p) =>
            {
                double sx = p.Dot(right);
                double sy = p.Dot(up);
                return new PointF((float)(cx + sx * scale), (float)(cy - sy * scale));
            };

            Bitmap bmp = new Bitmap(W, H);
            // Bitmap podporuje GetPixel/SetPixel; tu kreslíme cez Graphics (rýchlejšie).
            using (Graphics g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.Clear(Color.White);

                // --- súradnicové osi (voliteľné) ---
                if (chkAxes.Checked)
                {
                    DrawAxis(g, project, new Vec3(0, 0, 0), new Vec3(sceneRadius * 0.9, 0, 0), "X");
                    DrawAxis(g, project, new Vec3(0, 0, 0), new Vec3(0, sceneRadius * 0.9, 0), "Y");
                    DrawAxis(g, project, new Vec3(0, 0, 0), new Vec3(0, 0, sceneRadius * 0.55), "Z");
                }

                // --- plôšky plášťov: maliarsky algoritmus (zozadu dopredu) ---
                quads.Sort((q1, q2) =>
                {
                    double dep1 = ((q1.P0 + q1.P1 + q1.P2 + q1.P3) * 0.25).Dot(d);
                    double dep2 = ((q2.P0 + q2.P1 + q2.P2 + q2.P3) * 0.25).Dot(d);
                    return dep1.CompareTo(dep2);   // menšie (vzdialenejšie) prvé
                });

                foreach (Quad q in quads)
                {
                    double sh = 0.45 + 0.55 * Math.Max(0, q.Normal.Dot(light));
                    int gray = (int)Clamp(q.BaseGray * sh, 0, 255);
                    Color fill = Color.FromArgb(q.Alpha, gray, gray, gray);
                    PointF[] poly = { project(q.P0), project(q.P1), project(q.P2), project(q.P3) };
                    using (SolidBrush br = new SolidBrush(fill))
                        g.FillPolygon(br, poly);
                    using (Pen pn = new Pen(Color.FromArgb(q.Alpha, gray, gray, gray)))
                        g.DrawPolygon(pn, poly);   // potlačenie medzier medzi plôškami
                }

                // --- koncové kružnice (obrysové hrany) ---
                for (int k = 0; k < outlines.Count; k++)
                {
                    int gray = outlineGray[k];
                    using (Pen pn = new Pen(Color.FromArgb(200, gray, gray, gray), 1.2f))
                        DrawPolyline(g, project, outlines[k], pn);
                }

                // --- prieniková krivka: viditeľné (plná) a skryté (čiarkovaná) ---
                DrawIntersection(g, project, branch1, d);
                DrawIntersection(g, project, branch2, d);
            }

            Image old = pic.Image;
            pic.Image = bmp;
            if (old != null) old.Dispose();
        }

        // vykreslenie polyčiary
        private void DrawPolyline(Graphics g, Func<Vec3, PointF> proj, Vec3[] pts, Pen pen)
        {
            if (pts.Length < 2) return;
            PointF[] sp = new PointF[pts.Length];
            for (int i = 0; i < pts.Length; i++) sp[i] = proj(pts[i]);
            g.DrawLines(pen, sp);
        }

        // os so šípkou a popisom
        private void DrawAxis(Graphics g, Func<Vec3, PointF> proj, Vec3 a, Vec3 b, string name)
        {
            using (Pen pn = new Pen(Color.Black, 1f))
            {
                pn.CustomEndCap = new AdjustableArrowCap(4, 5);
                g.DrawLine(pn, proj(a), proj(b));
            }
            PointF lp = proj(b);
            g.DrawString(name, Font, Brushes.Black, lp.X + 2, lp.Y - 6);
        }

        // prieniková krivka so správnym typom čiary podľa viditeľnosti
        private void DrawIntersection(Graphics g, Func<Vec3, PointF> proj, List<Vec3> branch, Vec3 d)
        {
            if (branch.Count < 2) return;

            using (Pen solid = new Pen(Color.FromArgb(230, 0, 0), 3.4f))
            using (Pen dashed = new Pen(Color.FromArgb(230, 0, 0), 1.6f))
            {
                solid.StartCap = solid.EndCap = LineCap.Round;
                dashed.DashStyle = DashStyle.Dash;

                for (int i = 0; i < branch.Count - 1; i++)
                {
                    Vec3 p = branch[i], q = branch[i + 1];
                    // viditeľnosť stredu segmentu: normála (na menšom valci) ku kamere?
                    Vec3 mid = (p + q) * 0.5;
                    Vec3 n = (mid - bAxis * mid.Dot(bAxis)).Normalized;
                    bool visible = n.Dot(d) > 0;
                    if (!visible && !chkHidden.Checked) continue;
                    g.DrawLine(visible ? solid : dashed, proj(p), proj(q));
                }
            }
        }
    }

    // ==================================================================
    //  Vstupný bod aplikácie
    // ==================================================================
    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }
}

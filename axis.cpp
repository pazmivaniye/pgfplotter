#include "pgfplotter"
#include "system.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <filesystem>

static const std::string FontSize = "footnotesize";
static const std::string LegendFontSize = "scriptsize";
static const std::string TitleSize = "normalsize";
static const std::string Suffix = "_plot_data";
static constexpr unsigned int Precision = 10;

static const std::string CommonPreamble = 1 + R"===(
\IfFileExists{standalone.cls}{}{\errmessage{The "standalone" package is
    required.}}
\documentclass{standalone}
\usepackage{xstring}
\makeatletter
\@ifclasslater{standalone}{2018/03/26}{}{\usepackage{luatex85}}
% If newtx is new enough, use option `newsu`.
\IfFileExists{newtx.sty}%
{%
    \newread\myread
    \openin\myread=newtx.sty
    \@whilesw\ifx\mydone\undefined\fi%
    {%
        \readline\myread to \myline
        \StrGobbleLeft{\myline}{5}[\mytempa]
        \StrSplit{\mytempa}{8}{\mytempa}{\mytempb}
        \ifnum\pdf@strcmp{\mytempa}{filedate}=0\relax\def\mydone\fi
    }
    \closein\myread
    \makeatother
    \StrGobbleLeft{\myline}{14}[\newtxdate]
    \StrLeft{\newtxdate}{10}[\newtxdate]
    \StrSplit{\newtxdate}{4}{\newtxyear}{\newtxmonth}
    \StrGobbleLeft{\newtxmonth}{1}[\newtxmonth]
    \StrSplit{\newtxmonth}{2}{\newtxmonth}{\newtxday}
    \StrGobbleLeft{\newtxday}{1}[\newtxday]
    \def\usenewsu{1}
    \ifnum\newtxyear<2023\def\usenewsu{0}\fi
    \ifnum\newtxyear=2023\ifnum\newtxmonth<8\def\usenewsu{0}\fi\fi
    \ifnum\newtxyear=2023\ifnum\newtxmonth=8\ifnum\newtxday<21\def\usenewsu{0}%
        \fi\fi\fi
    \ifnum\usenewsu=1\usepackage[newsu]{newtx}\else\usepackage{newtx}\fi
}{%
    \usepackage{newtxtext}
    \usepackage{newtxmath}
}
\usepackage{bm}
\usepackage{tikz}
\usepackage{pgfplots}
\usepackage{xcolor}
\usepackage{siunitx}
\sisetup{per-mode = symbol, exponent-product = \ensuremath{\cdot},
    inter-unit-product = \ensuremath{\cdot}, group-separator = {,}, group-digits
    = integer}
\DeclareSIUnit[number-unit-product = ]\percent{\char`\%}
\DeclareSIUnit\GB{GB}
\DeclareSIUnit\arcsec{arcsec}
\DeclareSIUnit\au{AU}
\DeclareSIUnit\lu{LU}
\DeclareSIUnit\mu{MU}
\DeclareSIUnit\rev{rev}
\DeclareSIUnit\tu{TU}
\pgfplotsset{compat = 1.12}
\usetikzlibrary{pgfplots.groupplots}
\NewDocumentCommand\trans{}{\mathsf{T}}
\NewDocumentCommand\args{m}{\mathopen{}\left(\textstyle#1\right)}
\NewDocumentCommand\notimplies{}{\centernot\implies}
\NewDocumentCommand\prob{m}{\operatorname{P}\mathopen{}\left\{\textstyle#1%
    \right\}}
\NewDocumentCommand\expect{m}{\operatorname{E}\mathopen{}\left[\textstyle#1%
    \right]}
\NewDocumentCommand\given{}{\;\middle|\;}
\NewDocumentCommand\placeholder{}{\cdot}
\NewDocumentCommand\argmin{}{\operatornamewithlimits{arg\,min}}
\NewDocumentCommand\argmax{}{\operatornamewithlimits{arg\,max}}
\NewDocumentCommand\lab{m}{\operatorname{lab}\args{#1}}
\NewDocumentCommand\kronecker{mm}{\delta_{#1}\mathopen{}\left[\textstyle#2%
    \right]}
\NewDocumentCommand\dd{}{\operatorname{d}}
)===";
static const std::string src9 = []()
    {
        const std::size_t numColors = pgfplotter::Color::Defaults.size();
        std::ostringstream oss;
        for(std::size_t i = 0; i < numColors; ++i)
        {
            oss << "\\definecolor{color" << i << "}{RGB}{";
            for(int j = 0; j < 3; ++j)
            {
                oss << std::setw(3) << pgfplotter::Color::Defaults[i][j] << (j +
                    1 < 3 ? ", " : "");
            }
            oss << "}\n";
        }
        oss << "\\pgfplotscreateplotcyclelist{colorcycle}{";
        for(std::size_t i = 0; i < numColors; ++i)
        {
            oss << "{color" << i << "}" << (i + 1 < numColors ? ", " : "");
        }
        oss << "}\n";
        oss << "\\pgfplotsset{colormap = {bidir}{";
        for(int i = 0; i < 3; ++i)
        {
            oss << "rgb255 = (";
            for(int j = 0; j < 3; ++j)
            {
                oss << std::setw(3) << pgfplotter::Color::Bidir[i][j] << (j + 1
                    < 3 ? ", " : "");
            }
            oss << ")" << (i + 1 < 3 ? ", " : "");
        }
        oss << "}}\n";
        return oss.str();
    }();
static const std::string src2a = "\\begin{document}\n"
    "\\begin{tikzpicture}[define rgb/.code = {\\definecolor{mycolor}{RGB}{#1}},"
        " rgb color/.style = {define rgb = {#1}, mycolor}]\n"
    "\\begin{groupplot}[group style = {group name = subplots, columns = 1, rows"
        " = ";
static const std::string src2b = ", vertical sep = 1.3cm}]\n";
static const std::string src2bNoSep = ", vertical sep = 0.5cm}]\n";
// Axis options
static const std::string src1 = ", cycle list name = colorcycle"
    ", grid = major"
    ", minor tick num = 4"
    ", legend cell align = {left}";
static const std::string src8 = ", label style = {font = \\" + FontSize + "}"
    "]\n";
static const std::string src3 = "};\n";
static const std::string src4 = "\\end{groupplot}\n";
static const std::string src5 = "\\end{tikzpicture}\n\\end{document}";

static const std::string Makefile = 1 + R"===(
print-% : ; @echo "$* = $($*)"
plot.png: export TERM = dumb
plot.png: plot.pdf
	pdftoppm -png -r 300 plot.pdf > plot.png
	$(RM) plot.pdf
ifeq ($(OS), Windows_NT)
plot.pdf: plot.ps
	MSYS2_ARG_CONV_EXCL='*' ps2pdf14 -dPDFSETTINGS=/prepress plot.ps \
	    plot.pdf
	$(RM) plot.ps
else
plot.pdf: plot.ps
	ps2pdf14 -dPDFSETTINGS=/prepress plot.ps plot.pdf
	$(RM) plot.ps
endif
plot.ps: plot.tex $(wildcard *.data) $(wildcard *.surf)
	lualatex -halt-on-error -shell-escape -interaction=batchmode plot
	mv plot.pdf plot_unpressed.pdf
	$(RM) plot.{aux,log} plot_contourtmp*.{dat,script,table}
	pdf2ps plot_unpressed.pdf
	mv plot_unpressed.ps plot.ps
	$(RM) plot_unpressed.pdf
)===";

static std::string convert_marker(char marker)
{
    if(marker <= 0)
    {
        throw std::runtime_error("Tried to convert unprintable marker.");
    }
    if(marker == '^')
    {
        return "triangle";
    }
    if(marker == 's')
    {
        return "square";
    }
    if(marker == 'S')
    {
        return "square*";
    }
    if(marker == 'd')
    {
        return "square, mark options = {line join = miter, rotate = 45, scale ="
            " 0.6}";
    }
    if(marker == 'D')
    {
        return "square*, mark options = {line join = miter, rotate = 45, scale "
            "= 0.6}";
    }
    if(marker == 'x')
    {
        return "x, mark options = {line join = miter, scale = 1.5}";
    }
    return std::string() + marker;
}

static double floor_log(double x, double b)
{
    return std::pow(b, std::floor(std::log(x)/std::log(b)));
}

static double ceil_log(double x, double b)
{
    return std::pow(b, std::ceil(std::log(x)/std::log(b)));
}

// Write LuaLaTeX to a temporary file, compile and clean up.
static void compile(const std::string& path, const std::string& src, bool
    deleteData)
{
    if(path.find('"') != std::string::npos)
    {
        throw std::runtime_error("Plot path cannot contain double quote charact"
            "er.");
    }

    std::string dir;
    std::string name;
    pgfplotter::split_path(path, dir, name);

    if(name.find('\t') != std::string::npos || name.find(' ') != std::string::
        npos)
    {
        throw std::runtime_error("Plot name cannot contain whitespace.");
    }

    {
        const std::string texPath = path + Suffix + "/plot.tex";
        std::ofstream out(texPath);
        if(!out)
        {
            throw std::runtime_error("Unable to open output file \"" + texPath +
                "\".");
        }
        out << src << std::endl;
    }

    {
        const std::string makefilePath = path + Suffix + "/Makefile";
        std::ofstream out(makefilePath);
        if(!out)
        {
            throw std::runtime_error("Unable to open output file \"" +
                makefilePath + "\".");
        }
        out << Makefile;
    }

    try
    {
        pgfplotter::system_call("make", {"-C", path + Suffix});
    }
    catch(const std::exception& e)
    {
        std::cerr << "Warning: Failed to plot \"" << name << ".png\": " << e.
            what() << std::endl;
        return;
    }

    try
    {
        const std::string pngPath = path + Suffix + "/plot.png";
        const std::string newPath = (dir.empty() ? "." : dir) + "/" + name +
            ".png";
        std::filesystem::rename(pngPath, newPath);
    }
    catch(const std::exception& e)
    {
        std::cerr << "Warning: Failed to move \"" << name << ".png\": " << e.
            what() << std::endl;
        return;
    }

    std::cout << "Plotted \"" << path << ".png\"" << std::endl;

    if(deleteData)
    {
        try
        {
            std::filesystem::remove_all(path + Suffix);
        }
        catch(const std::exception& e)
        {
            std::cerr << "Warning: Failed to delete plot data for \"" << path <<
                "\": " << e.what() << std::endl;
        }
    }
    else
    {
        try
        {
            pgfplotter::system_call("zip", {"-jqr", path + Suffix + ".zip", path
                + Suffix});
            std::filesystem::remove_all(path + Suffix);
        }
        catch(const std::exception& e)
        {
            std::cerr << "Warning: Failed to archive and clean up plot data for"
                " \"" << path << "\": " << e.what() << std::endl;
        }
    }
}

static std::string to_string(double x)
{
    std::stringstream ss;
    ss << std::setprecision(Precision) << x;
    return ss.str();
}

const std::string& pgfplotter::Axis::title() const
{
    return _title;
}

void pgfplotter::Axis::setTitle(const std::string& title)
{
    _title = title;
}

const std::string& pgfplotter::Axis::xLabel() const
{
    return _xLabel;
}

void pgfplotter::Axis::setXLabel(const std::string& label)
{
    _xLabel = label;
}

const std::string& pgfplotter::Axis::yLabel() const
{
    return _yLabel;
}

void pgfplotter::Axis::setYLabel(const std::string& label)
{
    _yLabel = label;
}

const std::string& pgfplotter::Axis::zLabel() const
{
    return _zLabel;
}

void pgfplotter::Axis::setZLabel(const std::string& label)
{
    _zLabel = label;
}

const std::string& pgfplotter::Axis::wLabel() const
{
    return zLabel(); //TEMP - separate z & w
}

void pgfplotter::Axis::setWLabel(const std::string& label)
{
    setZLabel(label); //TEMP - separate z & w
}

const std::string& pgfplotter::Axis::groupLabel() const
{
    return _groupLabel;
}

void pgfplotter::Axis::setGroupLabel(const std::string& label)
{
    _groupLabel = label;
}

void pgfplotter::Axis::draw(const DrawStyle& style, const std::vector<double>&
    x, const std::vector<double>& y, const std::vector<double>& z, const std::
    vector<double>& w, const std::string& name)
{
    data.push_back({x, y, z, w});
    markers.push_back(style.markStyle);
    names.push_back(name);
    colors.push_back(style.color);
    lineStyles.push_back(style.lineStyle);
    lineWidths.push_back(style.lineWidth);
    opacities.push_back(style.opacity);
}

void pgfplotter::Axis::surf(const std::vector<double>& x, const std::vector<
    double>& y, const std::vector<double>& z, const std::string& name)
{
    surfaceX.push_back(x);
    surfaceY.push_back(y);
    surfaceZ.push_back(z);
    numContours.push_back(0);
    names.push_back(name);
    matrixSurf.push_back(false);
}

void pgfplotter::Axis::contour(const std::vector<double>& x, const std::vector<
    double>& y, const std::vector<double>& z, unsigned int contours, const std::
    string& name)
{
    surfaceX.push_back(x);
    surfaceY.push_back(y);
    surfaceZ.push_back(z);
    numContours.push_back(contours);
    names.push_back(name);
    matrixSurf.push_back(false);
}

void pgfplotter::Axis::matrix(const std::vector<double>& x, const std::vector<
    double>& y, const std::vector<double>& z, const std::string& name)
{
    surfaceX.push_back(x);
    surfaceY.push_back(y);
    surfaceZ.push_back(z);
    numContours.push_back(0);
    names.push_back(name);
    matrixSurf.push_back(true);
}

void pgfplotter::Axis::fill(const std::array<int, 3>& color, const std::vector<
    double>& x, const std::vector<double>& y)
{
    fillX.push_back(x);
    fillY.push_back(y);
    fillColors.push_back(color);
}

void pgfplotter::Axis::legend(unsigned int location)
{
    legendPos = location;
}

void pgfplotter::Axis::squeeze()
{
    xSqueeze = true;
    ySqueeze = true;
}

void pgfplotter::Axis::squeezeX()
{
    xSqueeze = true;
}

void pgfplotter::Axis::squeezeY()
{
    ySqueeze = true;
}

void pgfplotter::Axis::setXMin(double x)
{
    xMinSet = true;
    xMin = x;
}

void pgfplotter::Axis::setXMax(double x)
{
    xMaxSet = true;
    xMax = x;
}

void pgfplotter::Axis::setYMin(double y)
{
    yMinSet = true;
    yMin = y;
}

void pgfplotter::Axis::setYMax(double y)
{
    yMaxSet = true;
    yMax = y;
}

void pgfplotter::Axis::setZMin(double z)
{
    zMinSet = true;
    zMin = z;
}

void pgfplotter::Axis::setZMax(double z)
{
    zMaxSet = true;
    zMax = z;
}

void pgfplotter::Axis::setWMin(double w)
{
    setZMin(w); //TEMP - separate z & w
}

void pgfplotter::Axis::setWMax(double w)
{
    setZMax(w); //TEMP - separate z & w
}

void pgfplotter::Axis::axis_equal()
{
    axisEqual = true;
}

void pgfplotter::Axis::axis_equal_image()
{
    axisEqualImage = true;
}

void pgfplotter::Axis::resize(double width, double height)
{
    relWidth = width;
    relHeight = height;
}

void pgfplotter::Axis::resize(double size)
{
    relWidth = size;
    relHeight = size;
}

void pgfplotter::Axis::setXPrecision(int n)
{
    if(n < 0)
    {
        throw std::runtime_error("Precision cannot be negative.");
    }
    xPrecision = n;
}

void pgfplotter::Axis::setYPrecision(int n)
{
    if(n < 0)
    {
        throw std::runtime_error("Precision cannot be negative.");
    }
    yPrecision = n;
}

void pgfplotter::Axis::setZPrecision(int n)
{
    if(n < 0)
    {
        throw std::runtime_error("Precision cannot be negative.");
    }
    zPrecision = n;
}

void pgfplotter::Axis::setWPrecision(int n)
{
    setZPrecision(n); //TEMP - separate z & w
}

void pgfplotter::Axis::setXFormat(unsigned int mode)
{
    xFormat = mode;
}

void pgfplotter::Axis::setYFormat(unsigned int mode)
{
    yFormat = mode;
}

void pgfplotter::Axis::setZFormat(unsigned int mode)
{
    zFormat = mode;
}

void pgfplotter::Axis::setWFormat(unsigned int mode)
{
    zFormat = mode; //TEMP - separate z & w
}

void pgfplotter::Axis::setXLog(double base)
{
    _xLog = base;
}

void pgfplotter::Axis::setYLog(double base)
{
    _yLog = base;
}

void pgfplotter::Axis::setZLog(double base)
{
    _zLog = base;
}

void pgfplotter::Axis::showColorbar()
{
    _showColorbar = true;
}

void pgfplotter::Axis::scale_x_spacing(double n)
{
    xSpacing = n;
}

void pgfplotter::Axis::scale_y_spacing(double n)
{
    ySpacing = n;
}

void pgfplotter::Axis::scale_z_spacing(double n)
{
    zSpacing = n;
}

void pgfplotter::Axis::x_offset(double n)
{
    xOffset = n;
}

void pgfplotter::Axis::setView(double az, double el)
{
    _viewAngles = {az, el};
}

void pgfplotter::Axis::setSurfOpacity(double n)
{
    _opacity = n;
}

void pgfplotter::Axis::noSep()
{
    _noSep = true;
}

void pgfplotter::Axis::setXTicks(const std::vector<double>& locations, const
    std::vector<std::string>& labels, bool rotate)
{
    _xTicks = locations;
    _xTickLabels = labels;
    _rotateXTickLabels = rotate;
}

void pgfplotter::Axis::setYTicks(const std::vector<double>& locations, const
    std::vector<std::string>& labels)
{
    _yTicks = locations;
    _yTickLabels = labels;
}

void pgfplotter::Axis::setZTicks(const std::vector<double>& locations, const
    std::vector<std::string>& labels)
{
    _zTicks = locations;
    _zTickLabels = labels;
}

void pgfplotter::Axis::bgBands(const std::vector<double>& transitions)
{
    _bgBands = transitions;
}

void pgfplotter::Axis::bidirColormap()
{
    _bidirColormap = true;
}

std::string pgfplotter::Axis::plotSrc(const std::string& path, int subplot)
    const
{
    if(path.empty())
    {
        throw std::runtime_error("Plot name is empty.");
    }

    std::string dir;
    {
        std::string name;
        split_path(path, dir, name);
    }

    try
    {
        if(std::filesystem::exists(path + Suffix))
        {
            if(!std::filesystem::is_directory(path + Suffix))
            {
                throw std::runtime_error("Path already exists but is not a dire"
                    "ctory.");
            }
        }
        else
        {
            std::filesystem::create_directory(path + Suffix);
        }
    }
    catch(const std::exception& e)
    {
        throw std::runtime_error("Failed to create plot data directory \"" +
            path + Suffix + "\": " + e.what());
    }

    std::string src = "\\nextgroupplot[width = " + to_string(relWidth) + "\\tex"
        "twidth, height = " + to_string(relHeight) + "\\textwidth, colormap nam"
        "e = ";
    src += _bidirColormap ? "bidir" : "viridis";
    src += ", every axis plot/.append style = {ultra thick, line join = bevel, "
        "mark options = {line join = miter}}, view = {" + to_string(_viewAngles[
        0]) + "}{" + to_string(_viewAngles[1]) + "}, clip mode = individual, co"
        "lorbar style = {font = \\" + FontSize + ", y tick label style = {";
    if(zPrecision >= 0)
    {
        src += ", /pgf/number format/precision = " + to_string(zPrecision) +
            ", /pgf/number format/zerofill";
    }
    if(zFormat)
    {
        src += ", scaled y ticks = false";
    }
    if(zFormat == Fixed)
    {
        src += ", /pgf/number format/fixed, /pgf/number format/fixed zerofill ="
            " true";
    }
    else if(zFormat == Sci)
    {
        src += ", /pgf/number format/sci";
    }
    src += "}, label style = {font = \\" + FontSize + "}, ylabel near ticks";
    if(!_zLabel.empty())
    {
        src += ", ylabel = {" + _zLabel + "}";
    }
    src += "}";

    if(_xLog > 0.)
    {
        src += ", xmode = log, xtick distance = " + to_string(_xLog);
        if(_xLog != 10.)
        {
            src += ", log basis x = " + to_string(_xLog);
        }
    }
    if(_yLog > 0.)
    {
        src += ", ymode = log, ytick distance = " + to_string(_yLog);
        if(_yLog != 10.)
        {
            src += ", log basis y = " + to_string(_yLog);
        }
    }
    if(_zLog > 0.)
    {
        src += ", zmode = log, ztick distance = " + to_string(_zLog);
        if(_zLog != 10.)
        {
            src += ", log basis z = " + to_string(_zLog);
        }
    }

    if(xSpacing)
    {
        src += ", x coord trafo/.code = {\\pgfluamathparse{\\pgfmathresult/" +
            to_string(xSpacing) +"}}, x coord inv trafo/.code = {\\pgfluamathpa"
            "rse{\\pgfmathresult*" + to_string(xSpacing) + "}}";
    }
    else if(xOffset)
    {
        src += ", x coord trafo/.code = {\\pgfluamathparse{\\pgfmathresult - " +
            to_string(xOffset) +"}}, x coord inv trafo/.code = {\\pgfluamathpa"
            "rse{\\pgfmathresult + " + to_string(xOffset) + "}}";
    }
    if(ySpacing)
    {
        src += ", y coord trafo/.code = {\\pgfluamathparse{\\pgfmathresult/" +
            to_string(ySpacing) +"}}, y coord inv trafo/.code = {\\pgfluamathpa"
            "res{\\pgfmathresult*" + to_string(ySpacing) + "}}";
    }
    if(zSpacing)
    {
        src += ", z coord trafo/.code = {\\pgfluamathparse{\\pgfmathresult/" +
            to_string(zSpacing) +"}}, z coord inv trafo/.code = {\\pgfluamathpa"
            "res{\\pgfmathresult*" + to_string(zSpacing) + "}}";
    }

    if(_showColorbar)
    {
        src += ", colorbar";
    }

    double xMinData = std::numeric_limits<double>::max();
    double xMaxData = std::numeric_limits<double>::min();
    if(xSqueeze)
    {
        for(const auto& n : data)
        {
            if(!n[0].empty())
            {
                const auto xMinMax = std::minmax_element(n[0].begin(), n[0].
                    end());
                xMinData = std::min(xMinData, *(xMinMax.first));
                xMaxData = std::max(xMaxData, *(xMinMax.second));
            }
        }
    }

    double yMinData = std::numeric_limits<double>::max();
    double yMaxData = std::numeric_limits<double>::min();
    if(ySqueeze)
    {
        for(const auto& n : data)
        {
            if(!n[1].empty())
            {
                const auto yMinMax = std::minmax_element(n[1].begin(), n[1].
                    end());
                yMinData = std::min(yMinData, *(yMinMax.first));
                yMaxData = std::max(yMaxData, *(yMinMax.second));
            }
        }
    }

    double xMinPosData = std::numeric_limits<double>::max();
    double xMaxPosData = 0.;
    if(_xLog > 0.)
    {
        for(const auto& m : data)
        {
            for(auto n : m[0])
            {
                if(n > 0.)
                {
                    xMinPosData = std::min(xMinPosData, n);
                    xMaxPosData = std::max(xMaxPosData, n);
                }
            }
        }
    }

    double yMinPosData = std::numeric_limits<double>::max();
    double yMaxPosData = 0.;
    if(_yLog > 0.)
    {
        for(const auto& m : data)
        {
            for(auto n : m[1])
            {
                if(n > 0.)
                {
                    yMinPosData = std::min(yMinPosData, n);
                    yMaxPosData = std::max(yMaxPosData, n);
                }
            }
        }
    }

    double zMinPosData = std::numeric_limits<double>::max();
    double zMaxPosData = 0.;
    if(_zLog > 0.)
    {
        for(const auto& m : data)
        {
            for(auto n : m[2])
            {
                if(n > 0.)
                {
                    zMinPosData = std::min(zMinPosData, n);
                    zMaxPosData = std::max(zMaxPosData, n);
                }
            }
        }
    }

    if(xMinSet || xSqueeze)
    {
        src += ", xmin = " + to_string(xMinSet ? xMin : xMinData);
    }
    else if(xMaxPosData > 0.)
    {
        src += ", xmin = " + to_string(floor_log(xMinPosData, _xLog));
    }
    if(xMaxSet || xSqueeze)
    {
        src += ", xmax = " + to_string(xMaxSet ? xMax : xMaxData);
    }
    else if(xMaxPosData > 0.)
    {
        src += ", xmax = " + to_string(ceil_log(xMaxPosData, _xLog));
    }

    if(yMinSet || ySqueeze)
    {
        src += ", ymin = " + to_string(yMinSet ? yMin : yMinData);
    }
    else if(yMaxPosData > 0.)
    {
        src += ", ymin = " + to_string(floor_log(yMinPosData, _yLog));
    }
    if(yMaxSet || ySqueeze)
    {
        src += ", ymax = " + to_string(yMaxSet ? yMax : yMaxData);
    }
    else if(yMaxPosData > 0.)
    {
        src += ", ymax = " + to_string(ceil_log(yMaxPosData, _yLog));
    }

    // Z/meta max/min don't seem to affect contour placement in contour plots.
    if(zMinSet)
    {
        if(_viewAngles[0] != 0. || _viewAngles[1] != 90.)
        {
            src += ", zmin = " + to_string(zMin);
        }
        src += ", point meta min = " + to_string(zMin);
    }
    else if(zMaxPosData > 0.)
    {
        if(_viewAngles[0] != 0. || _viewAngles[1] != 90.)
        {
            src += ", zmin = " + to_string(floor_log(zMinPosData, _zLog));
        }
        src += ", point meta min = " + to_string(floor_log(zMinPosData, _zLog));
    }
    if(zMaxSet)
    {
        if(_viewAngles[0] != 0. || _viewAngles[1] != 90.)
        {
            src += ", zmax = " + to_string(zMax);
        }
        src += ", point meta max = " + to_string(zMax);
    }
    else if(zMaxPosData > 0.)
    {
        if(_viewAngles[0] != 0. || _viewAngles[1] != 90.)
        {
            src += ", zmax = " + to_string(ceil_log(zMaxPosData, _zLog));
        }
        src += ", point meta max = " + to_string(ceil_log(zMaxPosData, _zLog));
    }

    if(!_xLabel.empty())
    {
        src += ", xlabel = {" + _xLabel + "}";
    }
    if(!_yLabel.empty())
    {
        src += ", ylabel = {" + _yLabel + "}";
    }
    if(!_zLabel.empty())
    {
        src += ", zlabel = {" + _zLabel + "}";
    }
    if(!_title.empty())
    {
        src += ", title = {\\" + TitleSize + " " + _title + "}";
    }
    if(legendPos)
    {
        src += ", legend style = {font = \\" + LegendFontSize;
        if(legendPos == Northwest)
        {
            src += ", at = {(0, 1)}, anchor = north west";
        }
        else if(legendPos == Southwest)
        {
            src += ", at = {(0, 0)}, anchor = south west";
        }
        else if(legendPos == Southeast)
        {
            src += ", at = {(1, 0)}, anchor = south east";
        }
        else if(legendPos == Northeast)
        {
            src += ", at = {(1, 1)}, anchor = north east";
        }
        else
        {
            throw std::logic_error("Legend position " + std::to_string(
                legendPos) + " not recognized.");
        }
        src += ", legend style = {row sep = -2pt}}";
        src += ", legend image post style = {fill opacity = 1, draw opacity = 1"
            ", mark size = 2.}";
    }
    if(axisEqual)
    {
        src += ", axis equal";
    }
    else if(axisEqualImage)
    {
        src += ", axis equal image";
    }
    src += src1;
    if(!_viewAngles[0] && !_viewAngles[1])
    {
        src += ", xlabel near ticks, ylabel near ticks";
    }
    src += ", x tick label style = {font = \\" + FontSize;
    if(xPrecision >= 0)
    {
        src += ", /pgf/number format/precision = " + to_string(xPrecision) +
            ", /pgf/number format/zerofill";
    }
    if(xFormat)
    {
        src += ", scaled x ticks = false";
    }
    if(xFormat == Fixed)
    {
        src += ", /pgf/number format/fixed, /pgf/number format/fixed zerofill ="
            " true";
    }
    else if(xFormat == Sci)
    {
        src += ", /pgf/number format/sci";
    }
    if(_rotateXTickLabels)
    {
        src += ", rotate = 45, anchor = north east";
    }
    src += "}, y tick label style = {font = \\" + FontSize;
    if(yPrecision >= 0)
    {
        src += ", /pgf/number format/precision = " + to_string(yPrecision) +
            ", /pgf/number format/zerofill";
    }
    if(yFormat)
    {
        src += ", scaled y ticks = false";
    }
    if(yFormat == Fixed)
    {
        src += ", /pgf/number format/fixed, /pgf/number format/fixed zerofill ="
            " true";
    }
    else if(yFormat == Sci)
    {
        src += ", /pgf/number format/sci";
    }
    src += "}, z tick label style = {font = \\" + FontSize;
    if(zPrecision >= 0)
    {
        src += ", /pgf/number format/precision = " + to_string(zPrecision) +
            ", /pgf/number format/zerofill";
    }
    if(zFormat)
    {
        src += ", scaled z ticks = false";
    }
    if(zFormat == Fixed)
    {
        src += ", /pgf/number format/fixed, /pgf/number format/fixed zerofill ="
            " true";
    }
    else if(zFormat == Sci)
    {
        src += ", /pgf/number format/sci";
    }
    src += "}";
    if(!_xTicks.empty())
    {
        src += ", xtick = {";
        for(std::size_t i = 0; i < _xTicks.size(); ++i)
        {
            src += to_string(_xTicks[i]);
            if(i + 1 < _xTicks.size())
            {
                src += ", ";
            }
        }
        src += "}";
    }
    if(!_xTickLabels.empty())
    {
        src += ", xticklabels = {";
        for(std::size_t i = 0; i < _xTickLabels.size(); ++i)
        {
            src += _xTickLabels[i];
            if(i + 1 < _xTickLabels.size())
            {
                src += ", ";
            }
        }
        src += "}";
    }
    if(!_yTicks.empty())
    {
        src += ", ytick = {";
        for(std::size_t i = 0; i < _yTicks.size(); ++i)
        {
            src += to_string(_yTicks[i]);
            if(i + 1 < _yTicks.size())
            {
                src += ", ";
            }
        }
        src += "}";
    }
    if(!_yTickLabels.empty())
    {
        src += ", yticklabels = {";
        for(std::size_t i = 0; i < _yTickLabels.size(); ++i)
        {
            src += _yTickLabels[i];
            if(i + 1 < _yTickLabels.size())
            {
                src += ", ";
            }
        }
        src += "}";
    }
    if(!_zTicks.empty())
    {
        src += ", ztick = {";
        for(std::size_t i = 0; i < _zTicks.size(); ++i)
        {
            src += to_string(_zTicks[i]);
            if(i + 1 < _zTicks.size())
            {
                src += ", ";
            }
        }
        src += "}";
    }
    if(!_zTickLabels.empty())
    {
        src += ", zticklabels = {";
        for(std::size_t i = 0; i < _zTickLabels.size(); ++i)
        {
            src += _zTickLabels[i];
            if(i + 1 < _zTickLabels.size())
            {
                src += ", ";
            }
        }
        src += "}";
    }
    src += src8;

    if(!_bgBands.empty() && (!yMinSet || !yMaxSet || _bgBands.size()%2))
    {
        throw std::logic_error("BG BANDS NOT FULLY IMPLEMENTED");
    }
    for(std::size_t i = 0; i < _bgBands.size(); i += 2)
    {
        src += "\\fill[black, opacity = 0.1] (" + to_string(_bgBands[i]) + ", "
            + to_string(yMin) + ") rectangle (" + to_string(_bgBands[i + 1]) +
            ", " + to_string(yMax) + ");\n";
    }

    for(std::size_t i = 0, sz = surfaceX.size(); i < sz; ++i)
    {
        const std::size_t numPoints = surfaceX[i].size();
        if(surfaceY[i].size() != numPoints || surfaceZ[i].size() != numPoints)
        {
            throw std::runtime_error("Number of points in x, y, and z must matc"
                "h.");
        }

        std::size_t numRows = 1;
        for(std::size_t j = 1; j < numPoints; ++j)
        {
            if(surfaceX[i][j] == surfaceX[i][0])
            {
                ++numRows;
            }
            else
            {
                break;
            }
        }

        if(numContours[i])
        {
            src += "\\addplot3[contour gnuplot = {labels = false, number = " +
                std::to_string(numContours[i]);
            if(!dir.empty())
            {
                src += ", cmd = {cd '" + dir + "' && gnuplot \\\"\\script\\\"}";
            }
            src += "}, mesh/rows = " + std::to_string(numRows) + ", mesh/num po"
                "ints = " + std::to_string(numPoints) + "] table {";
        }
        else if(matrixSurf[i])
        {
            src += "\\addplot[matrix plot*, mesh/rows = " + std::to_string(
                numRows) + ", mesh/ordering = y varies, point meta = explicit] "
                "table[meta = z] {";
        }
        else
        {
            src += "\\addplot3[unbounded coords = jump, surf, mesh/rows = " +
                std::to_string(numRows) + ", mesh/ordering = y varies, shader ="
                " interp, opacity = " + to_string(_opacity) + ", z buffer = sor"
                "t] table {";
        }
        const std::string dataFile = std::to_string(subplot) + "." + std::
            to_string(i) + ".surf";
        src += dataFile + src3;
        const std::string dataPath = path + Suffix + "/" + dataFile;
        std::ofstream out(dataPath);
        if(!out)
        {
            throw std::runtime_error("Failed to open temporary output file \"" +
                dataPath + "\".");
        }
        out << "x y z" << std::endl;
        for(std::size_t j = 0; j < numPoints; ++j)
        {
            out << to_string(surfaceX[i][j]) << " " << to_string(surfaceY[i][j])
                << " " << to_string(surfaceZ[i][j]) << std::endl;
        }
    }

    for(std::size_t i = 0; i < fillX.size(); ++i)
    {
        const std::size_t numPoints = fillX[i].size();
        if(fillY[i].size() != numPoints)
        {
            throw std::runtime_error("Number of points in x and y must match.");
        }
        src += "\\fill[";
        if(fillColors[i][0] >= 0)
        {
            src += "rgb color = {" + std::to_string(fillColors[i][0]) + ", " +
                std::to_string(fillColors[i][1]) + ", " + std::to_string(
                fillColors[i][2]) + "}";
        }
        else
        {
            src += "black";
        }
        src += "] ";
        for(std::size_t j = 0; j < numPoints; ++j)
        {
            src += "(" + to_string(fillX[i][j]) + ", " + to_string(fillY[i][j])
                + ")--";
        }
        src += "cycle;\n";
    }

    for(std::size_t i = 0, sz = data.size(); i < sz; ++i)
    {
        const std::size_t numPoints = data[i][0].size();
        const bool is3D = !data[i][2].empty();
        const bool hasMeta = !data[i][3].empty();
        if(data[i][1].size() != numPoints)
        {
            throw std::runtime_error("Number of points in x and y must match.");
        }
        if(is3D && data[i][2].size() != numPoints)
        {
            throw std::runtime_error("Number of points in x and z must match.");
        }
        if(hasMeta && data[i][3].size() != numPoints)
        {
            throw std::runtime_error("Number of points in x and w must match.");
        }

        const bool hasLines = lineStyles[i] != LineStyle::None;

        src += is3D ? "\\addplot3+[" : "\\addplot+[";
        if(lineStyles[i] == LineStyle::Dashed)
        {
            src += "densely dashed, ";
        }
        else if(lineStyles[i] == LineStyle::Dotted)
        {
            src += "densely dotted, ";
        }
        else if(lineStyles[i] == LineStyle::None)
        {
            src += "only marks, ";
        }
        if(markers[i].mark > 0)
        {
            src += "mark = " + convert_marker(markers[i].mark) + ", mark size ="
                " " + to_string(3.*markers[i].size);
            if(markers[i].spacing)
            {
                src += ", mark repeat = " + std::to_string(markers[i].spacing);
            }
        }
        else if(markers[i].mark < 0 && MarkCycle(i).mark > 0)
        {
            src += "mark = " + convert_marker(MarkCycle(i).mark) + ", mark size"
                " = " + to_string(3.*MarkCycle(i).size*markers[i].size);
            if(markers[i].spacing)
            {
                src += ", mark repeat = " + std::to_string(markers[i].spacing);
            }
        }
        else
        {
            src += "mark = none";
        }
        if(colors[i][0] >= 0)
        {
            src += ", rgb color = {" + std::to_string(colors[i][0]) + ", " +
                std::to_string(colors[i][1]) + ", " + std::to_string(colors[i][
                2]) + "}";
        }
        else if(colors[i][0] == Color::FromW[0])
        {
            if(hasLines)
            {
                src += ", mesh, point meta = explicit, shader = interp";
            }
            if(markers[i].mark)
            {
                src += ", scatter, scatter src = explicit, scatter/use mapped c"
                    "olor = {draw = mapped color, fill = mapped color}";
            }
        }
        src += ", fill opacity = " + std::to_string(opacities[i]) +
            ", draw opacity = " + std::to_string(opacities[i]);
        if(lineWidths[i] != 1.)
        {
            src += ", line width = " + std::to_string(1.6*lineWidths[i]) + "pt";
        }
        const std::string dataFile = std::to_string(subplot) + "." + std::
            to_string(i) + ".data";
        src += "] table" + std::string(hasMeta ? "[meta = w]" : "") + " {" +
            dataFile + src3;
        const std::string dataPath = path + Suffix + "/" + dataFile;
        std::ofstream out(dataPath);
        if(!out)
        {
            throw std::runtime_error("Failed to open temporary output file \"" +
                dataPath + "\".");
        }
        out << "x y" << (is3D ? " z" : "") << (hasMeta ? " w" : "") << std::
            endl;
        for(std::size_t j = 0; j < numPoints; ++j)
        {
            out << to_string(data[i][0][j]) << " " << to_string(data[i][1][j])
                << (is3D ? " " + to_string(data[i][2][j]) : "") << (hasMeta ?
                " " + to_string(data[i][3][j]) : "") << std::endl;
        }
    }

    if(legendPos)
    {
        src += "\\legend{";
        for(const auto& n : names)
        {
            src += "{" + n + "}, ";
        }
        src += "}\n";
    }

    return src;
}

void pgfplotter::plot(const std::string& path, const std::vector<const
    pgfplotter::Axis*>& p)
{
    if(p.empty())
    {
        std::cerr << "Warning: No plots provided for \"" << path << ".png\"."
            << std::endl;
        return;
    }

    bool noSep = false;
    for(const auto& n : p)
    {
        if(n->_noSep)
        {
            noSep = true;
        }
        break;
    }

    std::string src = CommonPreamble + src9 + src2a + std::to_string(p.size()) +
        (noSep ? src2bNoSep : src2b);
    for(std::size_t i = 0, n = p.size(); i < n; ++i)
    {
        src += p[i]->plotSrc(path, i);
    }
    src += src4;
    if(!p[0]->_groupLabel.empty())
    {
        src += "\\path let ";
        for(std::size_t i = 0; i < p.size(); ++i)
        {
            src += "\\p" + std::to_string(i + 1) + " = (subplots c1r" + std::
                to_string(i + 1) + ".outer west)";
            if(i + 1 < p.size())
            {
                src += ",";
            }
            src += "\n    ";
        }
        src += "in coordinate (west) at (";
        if(p.size() == 1)
        {
            src += "{\\x1 - 0.25cm}, \\y1";
        }
        else
        {
            src += "{min(";
            for(std::size_t i = 0; i < p.size(); ++i)
            {
                src += "\\x" + std::to_string(i + 1);
                if(i + 1 < p.size())
                {
                    src += ", ";
                }
            }
            src += ") - 0.25cm}, {(";
            for(std::size_t i = 0; i < p.size(); ++i)
            {
                src += "\\y" + std::to_string(i + 1);
                if(i + 1 < p.size())
                {
                    src += " + ";
                }
            }
            src += ")/" + std::to_string(p.size()) + "}";
        }
        src += ");\n\\node[rotate = 90, font = \\" + FontSize + "] at (west) {"
            + p[0]->_groupLabel + "};\n";
    }
    src += src5;

    compile(path, src, false);
}

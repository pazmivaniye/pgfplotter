#include "pgfplotter"
#include "detect_os.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <filesystem>
#ifdef OS_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <cstring>
#endif

#ifdef OS_WINDOWS
static void system_call(const std::string& file, const std::vector<std::string>&
    args)
{
    std::string cmd = file;
    for(const auto& n : args)
    {
        cmd += " \"" + n + "\"";
    }
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = true;
    sa.lpSecurityDescriptor = nullptr;
    HANDLE g_hChildStd_IN_Rd = nullptr;
    HANDLE g_hChildStd_IN_Wr = nullptr;
    HANDLE g_hChildStd_OUT_Rd = nullptr;
    HANDLE g_hChildStd_OUT_Wr = nullptr;
    if(!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &sa, 0))
    {
        throw std::runtime_error("Failed to create output pipe: Error " + std::
            to_string(GetLastError()) + ".");
    }
    if(!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
    {
        throw std::runtime_error("Failed to set output pipe to inherit: Error "
            + std::to_string(GetLastError()) + ".");
    }
    if(!CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &sa, 0))
    {
        throw std::runtime_error("Failed to create input pipe: Error " + std::
            to_string(GetLastError()) + ".");
    }
    if(!SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0))
    {
        throw std::runtime_error("Failed to set input pipe to inherit: Error " +
            std::to_string(GetLastError()) + ".");
    }
    si.hStdError = g_hChildStd_OUT_Wr;
    si.hStdOutput = g_hChildStd_OUT_Wr;
    si.hStdInput = g_hChildStd_IN_Rd;
    si.dwFlags |= STARTF_USESTDHANDLES;
    PROCESS_INFORMATION pi;
    if(!CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()), nullptr,
        nullptr, true, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        throw std::runtime_error("Failed to create process: Error " + std::
            to_string(GetLastError()) + ".");
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode;
    if(!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        throw std::runtime_error("Failed to get exit code: Error " + std::
            to_string(GetLastError()) + ".");
    }
    if(exitCode == STILL_ACTIVE)
    {
        throw std::runtime_error("Wait returned before process completed.");
    }
    if(exitCode)
    {
        throw std::runtime_error("System call returned " + std::to_string(
            exitCode) + ".");
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(g_hChildStd_OUT_Wr);
    g_hChildStd_OUT_Wr = nullptr;
    CloseHandle(g_hChildStd_IN_Rd);
    g_hChildStd_OUT_Wr = nullptr;
}
#else
static void system_call(const std::string& file, const std::vector<std::string>&
    args)
{
    const auto pid = fork();
    if(pid > 0)
    {
        int status;
        if(waitpid(pid, &status, 0) < 0)
        {
            throw std::runtime_error("Wait failed.");
        }
        if(!WIFEXITED(status))
        {
            throw std::runtime_error("System call did not exit normally.");
        }
        const auto exitCode = WEXITSTATUS(status);
        if(exitCode)
        {
            throw std::runtime_error("System call returned " + std::to_string(
                exitCode) + ".");
        }
    }
    else if(pid == 0)
    {
        std::vector<const char*> argv = {file.c_str()};
        for(const auto& n : args)
        {
            argv.push_back(n.c_str());
        }
        argv.push_back(nullptr);
        const auto fd = open("/dev/null", O_WRONLY);
        if(fd < 0)
        {
            std::cerr << "Warning: Failed to open \"/dev/null\": " << std::
                strerror(errno) << std::endl;
        }
        else
        {
            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
        }
        const int status = execvp(argv[0], const_cast<char**>(argv.data()));
        std::exit(status);
    }
    else
    {
        throw std::runtime_error("Fork failed: " + std::string(std::strerror(
            errno)) + ".");
    }
}
#endif

static const std::string FontSize = "footnotesize";
static const std::string LegendFontSize = "scriptsize";
static const std::string TitleSize = "normalsize";

static const std::string Suffix = "_plot_data";

static std::string convert_marker(char marker)
{
    if(marker == '^')
    {
        return "triangle";
    }
    if(marker == '.')
    {
        return "*, mark options = {scale = 0.5}";
    }
    if(marker == 's')
    {
        return "square";
    }
    return std::string() + marker;
}

// Lines need to be broken up when using a temporary *.tex file because LaTeX
// can only handle input file lines below a certain length.
static const std::string endl = "\n";

// Convert numbers to strings without sacrificing precision.
std::string pgfplotter::Plotter::ToString(double x, unsigned int precision)
{
    std::stringstream ss;
    ss << std::setprecision(precision) << x;
    return ss.str();
}

// Extract directories from path.
static void split_path(const std::string& path, std::string& dir, std::string&
    name)
{
    const std::filesystem::path p(path);
    dir = p.parent_path().string();
    name = p.filename().string();
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
    split_path(path, dir, name);

    if(name.find('\t') != std::string::npos || name.find(' ') != std::string::
        npos)
    {
        throw std::runtime_error("Plot name cannot contain whitespace.");
    }

    {
        const std::string texPath = path + Suffix + "/" + name + ".tex";
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
        out << "print-% : ; @echo $* = $($*)" << std::endl << std::endl;
        out << name << ".png: export TERM = dumb" << std::endl;
        out << name << ".png: " << name << ".pdf" << std::endl;
        out << "\tpdftoppm -png -r 300 " << name << ".pdf > " << name << ".png "
            "\\" << std::endl;
        out << "\t    && $(RM) " << name << ".pdf" << std::endl;
        out << std::endl;
        out << "ifeq ($(OS), Windows_NT)" << std::endl;
        out << name << ".pdf: " << name << ".ps" << std::endl;
        out << "\tMSYS2_ARG_CONV_EXCL=\"*\" ps2pdf14 -dPDFSETTINGS=/prepress "
            << name << ".ps " << name << ".pdf \\" << std::endl;
        out << "\t    && $(RM) " << name << ".ps" << std::endl;
        out << "else" << std::endl;
        out << name << ".pdf: " << name << ".ps" << std::endl;
        out << "\tps2pdf14 -dPDFSETTINGS=/prepress " << name << ".ps " << name
            << ".pdf \\" << std::endl;
        out << "\t    && $(RM) " << name << ".ps" << std::endl;
        out << "endif" << std::endl;
        out << std::endl;
        out << name << ".ps: " << name << ".tex $(wildcard *.data) $(wildcard "
            "*.surf)" << std::endl;
        out << "\tlualatex -halt-on-error -shell-escape -interaction=batchmode "
            << name << " \\" << std::endl;
        out << "\t    && mv " << name << ".pdf " << name << "_unpressed.pdf \\"
            << std::endl;
        out << "\t    && $(RM) " << name << ".aux \\" << std::endl;
        out << "\t    && $(RM) " << name << ".log \\" << std::endl;
        out << "\t    && $(RM) " << name << "_contortmp*.dat \\" << std::endl;
        out << "\t    && $(RM) " << name << "_contortmp*.script \\" << std::
            endl;
        out << "\t    && $(RM) " << name << "_contortmp*.table \\" << std::endl;
        out << "\t    && pdf2ps " << name << "_unpressed.pdf \\" << std::endl;
        out << "\t    && mv " << name << "_unpressed.ps " << name << ".ps \\" <<
            std::endl;
        out << "\t    && $(RM) " << name << "_unpressed.pdf" << std::endl;
    }

    try
    {
        system_call("make", {"-C", path + Suffix});
    }
    catch(const std::exception& e)
    {
        std::cerr << "Warning: Failed to plot \"" << name << ".png\": " << e.
            what() << std::endl;
        return;
    }

    try
    {
        const std::string pngPath = path + Suffix + "/" + name + ".png";
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
            system_call("zip", {"-jqr", path + Suffix + ".zip", path + Suffix});
            std::filesystem::remove_all(path + Suffix);
        }
        catch(const std::exception& e)
        {
            std::cerr << "Warning: Failed to archive and clean up plot data for"
                " \"" << path << "\": " << e.what() << std::endl;
        }
    }
}

// Preamble
static const std::string src0 =
    "\\IfFileExists{standalone.cls}{}{\\errmessage{The \"standalone\" package i"
        "s required.}}" + endl +
    "\\documentclass"/*[varwidth]*/"{standalone}" + endl +
    "\\usepackage{xstring}" + endl +
    "\\makeatletter" + endl +
    "\\@ifclasslater{standalone}{2018/03/26}{}{\\usepackage{luatex85}}" + endl +
    // If `newtx` is new enough, use option `newsu`.
    "\\newread\\myread" + endl +
    "\\openin\\myread=newtx.sty" + endl +
    "\\@whilesw\\ifx\\mydone\\undefined\\fi%" + endl +
    "{%" + endl +
    "    \\readline\\myread to \\myline" + endl +
    "    \\StrGobbleLeft{\\myline}{5}[\\mytempa]" + endl +
    "    \\StrSplit{\\mytempa}{8}{\\mytempa}{\\mytempb}" + endl +
    "    \\ifnum\\pdf@strcmp{\\mytempa}{filedate}=0\\relax\\def\\mydone\\fi" +
        endl +
    "}" + endl +
    "\\closein\\myread" + endl +
    "\\makeatother" + endl +
    "\\StrGobbleLeft{\\myline}{14}[\\newtxdate]" + endl +
    "\\StrLeft{\\newtxdate}{10}[\\newtxdate]" + endl +
    "\\StrSplit{\\newtxdate}{4}{\\newtxyear}{\\newtxmonth}" + endl +
    "\\StrGobbleLeft{\\newtxmonth}{1}[\\newtxmonth]" + endl +
    "\\StrSplit{\\newtxmonth}{2}{\\newtxmonth}{\\newtxday}" + endl +
    "\\StrGobbleLeft{\\newtxday}{1}[\\newtxday]" + endl +
    "\\def\\usenewsu{1} \\ifnum\\newtxyear<2023\\def\\usenewsu{0}\\fi \\ifnum\\"
        "newtxyear=2023\\ifnum\\newtxmonth<8\\def\\usenewsu{0}\\fi\\fi \\ifnum"
        "\\newtxyear=2023\\ifnum\\newtxmonth=8\\ifnum\\newtxday<21\\def\\usenew"
        "su{0}\\fi\\fi\\fi" + endl +
#if 1 // AIAA
    "\\ifnum\\usenewsu=1\\usepackage[newsu]{newtx}\\else\\usepackage{newtx}\\fi"
        + endl +
#elif 0 // AAS
    "\\ifnum\\usenewsu=1\\usepackage[newsu]{newtx}\\else\\usepackage{newtxtext}"
        "\\fi" + endl +
    "\\usepackage{amsmath}" + endl +
    "\\usepackage{amsfonts}" + endl +
#endif
    "\\usepackage{bm}" + endl +
    "\\usepackage{tikz}" + endl +
    "\\usepackage{pgfplots}" + endl +
    "\\usepackage{xcolor}" + endl +
    "\\usepackage{siunitx}" + endl +
    "\\sisetup{exponent-product = \\ensuremath{\\cdot}, inter-unit-product = \\"
        "ensuremath{\\cdot}, group-separator = {,}, group-digits = integer, per"
        "-mode = symbol}" + endl +
    "\\DeclareSIUnit[number-unit-product = ]\\percent{\\char`\\%}" + endl +
    "\\DeclareSIUnit\\au{AU}" + endl +
    "\\DeclareSIUnit\\lu{LU}" + endl +
    "\\DeclareSIUnit\\tu{TU}" + endl +
    "\\DeclareSIUnit\\mu{MU}" + endl +
    "\\DeclareSIUnit\\arcsec{arcsec}" + endl +
    "\\pgfplotsset{compat = 1.12}" + endl +
    "\\usetikzlibrary{pgfplots.groupplots}" + endl +
    "\\NewDocumentCommand\\trans{}{\\mathsf{T}}" + endl +
    "\\NewDocumentCommand\\args{m}{\\mathchoice{\\!\\left(#1\\right)}{\\!\\left"
        "(#1\\right)}{\\left(#1\\right)}{\\left(#1\\right)}}" + endl +
    "\\NewDocumentCommand\\notimplies{}{\\centernot\\implies}" + endl +
    "\\NewDocumentCommand\\prob{m}{\\operatorname{P}\\!\\left\\{#1\\right\\}}" +
        endl +
    "\\NewDocumentCommand\\expect{m}{\\operatorname{E}\\!\\left[#1\\right]}" +
        endl +
    "\\NewDocumentCommand\\given{}{\\;\\middle|\\;}" + endl +
    "\\NewDocumentCommand\\placeholder{}{\\cdot}" + endl +
    "\\NewDocumentCommand\\argmax{}{\\operatornamewithlimits{arg\\,max}}" + endl
        +
    "\\NewDocumentCommand\\lab{}{\\operatorname{lab}}" + endl +
    "\\NewDocumentCommand\\kronecker{mm}{\\delta_{#1}\\!\\left[#2\\right]}" +
        endl +
    "\\NewDocumentCommand\\dd{}{\\operatorname{d}}" + endl +
    "\\NewDocumentCommand\\dv{mm}{\\frac{\\dd#1}{\\dd#2}}" + endl +
    "\\NewDocumentCommand\\pdv{mm}{\\frac{\\partial#1}{\\partial#2}}" + endl +
    "\\definecolor{color0}{RGB}{  0,  57, 181}" + endl +
    "\\definecolor{color1}{RGB}{202,  46,   0}" + endl +
    "\\definecolor{color2}{RGB}{ 44, 151, 225}" + endl +
    "\\definecolor{color3}{RGB}{141, 202,  58}" + endl +
    "\\definecolor{color4}{RGB}{255, 227,   0}" + endl +
    "\\pgfplotscreateplotcyclelist{colorcycle}{{color0}, {color1}, {color2}, {c"
        "olor3}, {color4}}" + endl +
    "\\pgfplotsset{colormap = {bidir}{rgb255 = (10, 10, 143), rgb255 = (255, 25"
        "5, 255), rgb255 = (160, 10, 10)}}" + endl +
    "\\begin{document}" + endl;
static const std::string src2a = "\\begin{tikzpicture}[define rgb/.code = {\\de"
    "finecolor{mycolor}{RGB}{#1}}"
    ", rgb color/.style = {define rgb = {#1}, mycolor}"
    "]" + endl +
    "\\begin{groupplot}[group style = {columns = 1, rows = ";
static const std::string src2b = ", vertical sep = 1.3cm}]" + endl;
static const std::string src2bNoSep = ", vertical sep = 0.5cm}]" + endl;
// Axis options
static const std::string src1 = ", cycle list name = colorcycle"
    ", grid = major"
    ", minor tick num = 4"
    ", legend cell align = {left}";
static const std::string src8 = ", label style = {font = \\" + FontSize + "}"
    "]" + endl;
static const std::string src3 = "};" + endl;
static const std::string src4 = "\\end{groupplot}" + endl +
    "\\end{tikzpicture}" + endl;
static const std::string src5 = "\\end{document}";
static const std::string src7 = "" + endl;

void pgfplotter::Plotter::setTitle(const std::string& title)
{
    _title = title;
}

void pgfplotter::Plotter::setXLabel(const std::string& xLabel)
{
    _xLabel = xLabel;
}

void pgfplotter::Plotter::setYLabel(const std::string& yLabel)
{
    _yLabel = yLabel;
}

void pgfplotter::Plotter::setZLabel(const std::string& zLabel)
{
    _zLabel = zLabel;
}

void pgfplotter::Plotter::line(const std::vector<double>& x, const std::vector<double>& y,
    const std::vector<double>& z, const std::string& name, const std::array<int,
    3>& color, double width)
{
    data.push_back({x, y, z});
    markers.emplace_back();
    names.push_back(name);
    colors.push_back(color);
    lineStyles.push_back(0);
    lineWidths.push_back(width);
    opacities.push_back(1.);
}

void pgfplotter::Plotter::dashed(const std::vector<double>& x, const std::vector<double>& y,
    const std::string& name, const std::array<int, 3>& color)
{
    data.push_back({x, y, {}});
    markers.emplace_back();
    names.push_back(name);
    colors.push_back(color);
    lineStyles.push_back(1);
    lineWidths.push_back(1.);
    opacities.push_back(1.);
}

void pgfplotter::Plotter::dotted(const std::vector<double>& x, const std::vector<double>& y,
    const std::string& name, const std::array<int, 3>& color)
{
    data.push_back({x, y, {}});
    markers.emplace_back();
    names.push_back(name);
    colors.push_back(color);
    lineStyles.push_back(2);
    lineWidths.push_back(1.);
    opacities.push_back(1.);
}

void pgfplotter::Plotter::scatter(const std::vector<double>& x, const std::vector<double>&
    y, const std::vector<double>& z, char marker, const std::string& name, const
    std::array<int, 3>& color, double opacity)
{
    data.push_back({x, y, z});
    markers.push_back(convert_marker(marker));
    names.push_back(name);
    colors.push_back(color);
    lineStyles.push_back(0);
    lineWidths.push_back(1.);
    opacities.push_back(opacity);
}

void pgfplotter::Plotter::polar_line(const std::vector<double>& r, const std::vector<
    double>& theta, const std::string& name, const std::array<int, 3>& color)
{
    const std::size_t n = theta.size();
    std::vector<double> x(n);
    std::vector<double> y(n);
    for(std::size_t i = 0; i < n; ++i)
    {
        x[i] = r[i]*std::cos(theta[i]);
        y[i] = r[i]*std::sin(theta[i]);
    }
    data.push_back({x, y, {}});
    markers.emplace_back();
    names.push_back(name);
    colors.push_back(color);
    lineStyles.push_back(0);
    lineWidths.push_back(1.);
    opacities.push_back(1.);
}

void pgfplotter::Plotter::polar_scatter(const std::vector<double>& r, const std::vector<
    double>& theta, char marker, const std::string& name, const std::array<int,
    3>& color)
{
    const std::size_t n = theta.size();
    std::vector<double> x(n);
    std::vector<double> y(n);
    for(std::size_t i = 0; i < n; ++i)
    {
        x[i] = r[i]*std::cos(theta[i]);
        y[i] = r[i]*std::sin(theta[i]);
    }
    data.push_back({x, y, {}});
    markers.push_back(convert_marker(marker));
    names.push_back(name);
    colors.push_back(color);
    lineStyles.push_back(0);
    lineWidths.push_back(1.);
    opacities.push_back(1.);
}

void pgfplotter::Plotter::stairs(const std::vector<double>& x, const std::vector<double>& y,
    const std::string& name, const std::array<int, 3>& color)
{
    const std::size_t n = x.size();
    std::vector<double> x1(2*n);
    std::vector<double> y1(2*n);
    y1[0] = y[0];
    for(std::size_t i = 0; i + 1 < n; ++i)
    {
        y1[2*i + 1] = y[i];
        y1[2*i + 2] = y[i];
    }
    y1[2*n - 1] = y[n - 1];
    for(std::size_t i = 0; i < n; ++i)
    {
        x1[2*i] = x[i];
        x1[2*i + 1] = x[i];
    }

    line(x1, y1, name, color);
}

void pgfplotter::Plotter::dashed_stairs(const std::vector<double>& x, const std::vector<
    double>& y, const std::string& name, const std::array<int, 3>& color)
{
    const std::size_t n = x.size();
    std::vector<double> x1(2*n);
    std::vector<double> y1(2*n);
    y1[0] = y[0];
    for(std::size_t i = 0; i + 1 < n; ++i)
    {
        y1[2*i + 1] = y[i];
        y1[2*i + 2] = y[i];
    }
    y1[2*n - 1] = y[n - 1];
    for(std::size_t i = 0; i < n; ++i)
    {
        x1[2*i] = x[i];
        x1[2*i + 1] = x[i];
    }

    dashed(x1, y1, name, color);
}

void pgfplotter::Plotter::surf(const std::vector<double>& x, const std::vector<double>& y,
    const std::vector<double>& z, const std::string& name)
{
    surfaceX.push_back(x);
    surfaceY.push_back(y);
    surfaceZ.push_back(z);
    numContours.push_back(0);
    names.push_back(name);
    matrixSurf.push_back(false);
}

void pgfplotter::Plotter::contour(const std::vector<double>& x, const std::vector<double>&
    y, const std::vector<double>& z, unsigned int contours, const std::string&
    name)
{
    surfaceX.push_back(x);
    surfaceY.push_back(y);
    surfaceZ.push_back(z);
    numContours.push_back(contours);
    names.push_back(name);
    matrixSurf.push_back(false);
}

void pgfplotter::Plotter::matrix(const std::vector<double>& x, const std::vector<double>& y,
    const std::vector<double>& z, const std::string& name)
{
    surfaceX.push_back(x);
    surfaceY.push_back(y);
    surfaceZ.push_back(z);
    numContours.push_back(0);
    names.push_back(name);
    matrixSurf.push_back(true);
}

//TEMP
void pgfplotter::Plotter::circles(const std::vector<double>& x, const std::vector<double>&
    y, const std::vector<double>& r)
{
    circleX.insert(circleX.end(), x.begin(), x.end());
    circleY.insert(circleY.end(), y.begin(), y.end());
    circleR.insert(circleR.end(), r.begin(), r.end());
}
//TEMP

void pgfplotter::Plotter::legend(unsigned int location)
{
    legendPos = location;
}

void pgfplotter::Plotter::squeeze()
{
    xSqueeze = true;
    ySqueeze = true;
}

void pgfplotter::Plotter::squeeze_x()
{
    xSqueeze = true;
}

void pgfplotter::Plotter::squeeze_y()
{
    ySqueeze = true;
}

void pgfplotter::Plotter::x_min(double x)
{
    xMinSet = true;
    xMin = x;
}

void pgfplotter::Plotter::x_max(double x)
{
    xMaxSet = true;
    xMax = x;
}

void pgfplotter::Plotter::y_min(double y)
{
    yMinSet = true;
    yMin = y;
}

void pgfplotter::Plotter::y_max(double y)
{
    yMaxSet = true;
    yMax = y;
}

void pgfplotter::Plotter::z_min(double z)
{
    zMinSet = true;
    zMin = z;
}

void pgfplotter::Plotter::z_max(double z)
{
    zMaxSet = true;
    zMax = z;
}

void pgfplotter::Plotter::axis_equal()
{
    axisEqual = true;
}

void pgfplotter::Plotter::axis_equal_image()
{
    axisEqualImage = true;
}

void pgfplotter::Plotter::resize(double width, double height)
{
    relWidth = width;
    relHeight = height;
}

void pgfplotter::Plotter::resize(double size)
{
    relWidth = size;
    relHeight = size;
}

void pgfplotter::Plotter::x_precision(int n)
{
    if(n < 0)
    {
        throw std::runtime_error("Precision cannot be negative.");
    }
    xPrecision = n;
}

void pgfplotter::Plotter::y_precision(int n)
{
    if(n < 0)
    {
        throw std::runtime_error("Precision cannot be negative.");
    }
    yPrecision = n;
}

void pgfplotter::Plotter::z_precision(int n)
{
    if(n < 0)
    {
        throw std::runtime_error("Precision cannot be negative.");
    }
    zPrecision = n;
}

void pgfplotter::Plotter::x_format(unsigned int mode)
{
    xFormat = mode;
}

void pgfplotter::Plotter::y_format(unsigned int mode)
{
    yFormat = mode;
}

void pgfplotter::Plotter::z_format(unsigned int mode)
{
    zFormat = mode;
}

void pgfplotter::Plotter::x_log()
{
    xLog = true;
}

void pgfplotter::Plotter::y_log()
{
    yLog = true;
}

void pgfplotter::Plotter::z_log()
{
    zLog = true;
}

void pgfplotter::Plotter::colorbar()
{
    showColorbar = true;
}

void pgfplotter::Plotter::scale_x_spacing(double n)
{
    xSpacing = n;
}

void pgfplotter::Plotter::scale_y_spacing(double n)
{
    ySpacing = n;
}

void pgfplotter::Plotter::scale_z_spacing(double n)
{
    zSpacing = n;
}

void pgfplotter::Plotter::x_offset(double n)
{
    xOffset = n;
}

void pgfplotter::Plotter::view(double a, double b)
{
    viewAngles = {a, b};
}

void pgfplotter::Plotter::opacity(double n)
{
    _opacity = n;
}

void pgfplotter::Plotter::noSep()
{
    _noSep = true;
}

void pgfplotter::Plotter::xTicks(const std::vector<double>& locations, const std::vector<
    std::string>& labels, bool rotate)
{
    _xTicks = locations;
    _xTickLabels = labels;
    _rotateXTickLabels = rotate;
}

void pgfplotter::Plotter::bgBands(const std::vector<double>& transitions)
{
    _bgBands = transitions;
}

void pgfplotter::Plotter::bidirColormap()
{
    _bidirColormap = true;
}

std::string pgfplotter::Plotter::plot_src(const std::string& path, int subplot) const
{
    if(path.empty())
    {
        throw std::runtime_error("Plot name is empty.");
    }

    std::string dir;
    {
        std::string tempName;
        split_path(path, dir, tempName);
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

    std::string src = "\\nextgroupplot[width = " + ToString(relWidth) + "\\text"
        "width, height = " + ToString(relHeight) + "\\textwidth, colormap name "
        "= ";
    src += _bidirColormap ? "bidir" : "viridis";
    src += ", every axis plot/.append style = {ultra thick}, view = {" +
        ToString(viewAngles[0]) + "}{" + ToString(viewAngles[1]) + "}, clip mod"
        "e = individual, colorbar style = {font = \\" + FontSize + ", y tick la"
        "bel style = {";
    if(zPrecision >= 0)
    {
        src += ", /pgf/number format/precision = " + ToString(zPrecision) +
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

    if(xLog)
    {
        src += ", xmode = log";
    }
    if(yLog)
    {
        src += ", ymode = log";
    }
    if(zLog)
    {
        src += ", zmode = log";
    }

    if(xSpacing)
    {
        src += ", x coord trafo/.code = {\\pgfluamathparse{\\pgfmathresult/" +
            ToString(xSpacing) +"}}, x coord inv trafo/.code = {\\pgfluamathpa"
            "rse{\\pgfmathresult*" + ToString(xSpacing) + "}}";
    }
    else if(xOffset)
    {
        src += ", x coord trafo/.code = {\\pgfluamathparse{\\pgfmathresult - " +
            ToString(xOffset) +"}}, x coord inv trafo/.code = {\\pgfluamathpa"
            "rse{\\pgfmathresult + " + ToString(xOffset) + "}}";
    }
    if(ySpacing)
    {
        src += ", y coord trafo/.code = {\\pgfluamathparse{\\pgfmathresult/" +
            ToString(ySpacing) +"}}, y coord inv trafo/.code = {\\pgfluamathpa"
            "res{\\pgfmathresult*" + ToString(ySpacing) + "}}";
    }
    if(zSpacing)
    {
        src += ", z coord trafo/.code = {\\pgfluamathparse{\\pgfmathresult/" +
            ToString(zSpacing) +"}}, z coord inv trafo/.code = {\\pgfluamathpa"
            "res{\\pgfmathresult*" + ToString(zSpacing) + "}}";
    }

    if(showColorbar)
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

    if(xMinSet || xSqueeze)
    {
        src += ", xmin = " + ToString(xMinSet ? xMin : xMinData);
    }
    if(xMaxSet || xSqueeze)
    {
        src += ", xmax = " + ToString(xMaxSet ? xMax : xMaxData);
    }

    const double tempYMin = yMinSet ? yMin : yMinData;
    const double tempYMax = yMaxSet ? yMax : yMaxData;
    //const double yRange = tempYMax - tempYMin;
    if(yMinSet || ySqueeze)
    {
        src += ", ymin = " + ToString(tempYMin);
    }
    if(yMaxSet || ySqueeze)
    {
        src += ", ymax = " + ToString(tempYMax);
    }
    /*if((yMinSet && yMaxSet) || ySqueeze) // Disables Lua backend if mesh plot
    {
        src += ", restrict y to domain = " + ToString(tempYMin - 10.0*yRange) +
            ":" + ToString(tempYMax + 10.0*yRange);
    }*/

    // Z/meta max/min don't seem to affect contour placement in contour plots.
    if(zMinSet)
    {
        if(viewAngles[0] != 0. || viewAngles[1] != 90.)
        {
            src += ", zmin = " + ToString(zMin);
        }
        src += ", point meta min = " + ToString(zMin);
    }
    if(zMaxSet)
    {
        if(viewAngles[0] != 0. || viewAngles[1] != 90.)
        {
            src += ", zmax = " + ToString(zMax);
        }
        src += ", point meta max = " + ToString(zMax);
    }
    /*if(zMinSet && zMaxSet)
    {
        src += ", restrict z to domain = " + ToString(zMin) + ":" + ToString(
            zMax);//TEMP
    }*/

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
    if(!viewAngles[0] && !viewAngles[1])
    {
        src += ", xlabel near ticks, ylabel near ticks";
    }
    src += ", x tick label style = {font = \\" + FontSize;
    if(xPrecision >= 0)
    {
        src += ", /pgf/number format/precision = " + ToString(xPrecision) +
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
        src += ", /pgf/number format/precision = " + ToString(yPrecision) +
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
        src += ", /pgf/number format/precision = " + ToString(zPrecision) +
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
            src += ToString(_xTicks[i]);
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
    src += src8;

    if(!_bgBands.empty() && (!yMinSet || !yMaxSet || _bgBands.size()%2))
    {
        throw std::logic_error("BG BANDS NOT FULLY IMPLEMENTED");
    }
    for(std::size_t i = 0; i < _bgBands.size(); i += 2)
    {
        src += "\\fill[black, opacity = 0.1] (" + ToString(_bgBands[i]) + ", " +
            ToString(yMin) + ") rectangle (" + ToString(_bgBands[i + 1]) + ", "
            + ToString(yMax) + ");" + endl;
    }

    for(std::size_t i = 0, sz = surfaceX.size(); i < sz; ++i)
    {
        const std::size_t numPoints = surfaceX[i].size();
        if(surfaceY[i].size() != numPoints || surfaceZ[i].size() != numPoints)
        {
            throw std::runtime_error("Number of points in x, y and z must match"
                ".");
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
                " interp, opacity = " + ToString(_opacity) + ", z buffer = sort"
                "] table {";
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
            out << ToString(surfaceX[i][j]) << " " << ToString(surfaceY[i][j])
                << " " << ToString(surfaceZ[i][j]) << std::endl;
        }
    }

    for(std::size_t i = 0, sz = data.size(); i < sz; ++i)
    {
        const std::size_t numPoints = data[i][0].size();
        const bool is3D = !data[i][2].empty();
        if(data[i][1].size() != numPoints)
        {
            throw std::runtime_error("Number of points in x and y must match.");
        }
        if(is3D && data[i][2].size() != numPoints)
        {
            throw std::runtime_error("Number of points in x and z must match.");
        }

        src += is3D ? "\\addplot3+[" : "\\addplot+[";
        if(lineStyles[i] == 1)
        {
            src += "densely dashed, ";
        }
        else if(lineStyles[i] == 2)
        {
            src += "densely dotted, ";
        }
        /*if(names[i].empty())
        {
            src += "forget plot, ";
        }*/
        if(!markers[i].empty())
        {
            src += "mark = " + markers[i] + ", mark size = 3, only marks";
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
        src += ", fill opacity = " + std::to_string(opacities[i]) +
            ", draw opacity = " + std::to_string(opacities[i]);
        if(lineWidths[i] != 1.)
        {
            src += ", line width = " + std::to_string(1.6*lineWidths[i]) + "pt";
        }
        const std::string dataFile = std::to_string(subplot) + "." + std::
            to_string(i) + ".data";
        src += "] table {" + dataFile + src3;
        const std::string dataPath = path + Suffix + "/" + dataFile;
        std::ofstream out(dataPath);
        if(!out)
        {
            throw std::runtime_error("Failed to open temporary output file \"" +
                dataPath + "\".");
        }
        out << "x y" << (is3D ? " z" : "") << std::endl;
        for(std::size_t j = 0; j < numPoints; ++j)
        {
            out << ToString(data[i][0][j]) << " " << ToString(data[i][1][j]) <<
                (is3D ? " " + ToString(data[i][2][j]) : "") << std::endl;
        }
    }

    if(legendPos)// && names.size() == data.size())
    {
        src += "\\legend{";
        for(const auto& n : names)
        {
//            if(!n.empty())
            {
                src += n + ", ";
            }
        }
        src += "}" + endl;
    }

    //TEMP
    for(std::size_t i = 0; i < circleX.size(); ++i)
    {
        src += "\\draw (axis cs:" + ToString(circleX[i]) + ", " + ToString(
            circleY[i]) + ") circle [radius = " + ToString(circleR[i]) + "];" +
            endl;
    }
    //TEMP

    return src;
}

void pgfplotter::plot(const std::string& path, const std::vector<const
    pgfplotter::Plotter*>& p)
{
    if(p.empty())
    {
        std::cerr << "Warning: No plots provided for \"" << path << ".png\"."
            << std::endl;
        return;
    }

    bool b = false;
    for(const auto& n : p)
    {
        b = b || n->_noSep;
    }

    std::string src = src0 + src2a + std::to_string(p.size()) + (b ? src2bNoSep
        : src2b);
    for(std::size_t i = 0, n = p.size(); i < n; ++i)
    {
        src += p[i]->plot_src(path, i);
    }
    src += src4 + src5;

    compile(path, src, false);
}

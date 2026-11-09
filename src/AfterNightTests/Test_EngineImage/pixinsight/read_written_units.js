// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
//
// Reads, in PixInsight, the XISF units the engine's writer wrote, and holds what PixInsight reads to what was written.
//
// XisfInteropTest.EveryFormTheWriterProducesIsWrittenAsStatedForAnotherReader in Test_EngineImage writes the units and
// their catalogue, units.json, into the directory AFTERNIGHT_XISF_WRITTEN_UNITS names. Each unit is opened through
// PixInsight's XISF module and compared:
//
// - its geometry, sample format, colour space and image type with the catalogue's;
// - every sample: a unit of the written pattern with the pattern, which this script computes as the test does, every
//   integer sample to its code and every floating point sample exactly; a unit of PixInsight's written again with
//   PixInsight's own reading of the original, sample for sample;
// - its FITS keywords and its properties, each with its type and value, with those the catalogue lists, or with those
//   of the original;
// - its colour filter array, its identifier, resolution and display function where the catalogue states them;
// - its astrometric solution, opened in an image window: where PixInsight puts each point of a grid, with where the
//   engine put it, or with where PixInsight puts it in the original.
//
// Run it in automation mode, the arguments being the directory of the units, the directory of PixInsight's originals
// (src/AfterNightTests/fixtures/common/xisf) and the report to write:
//
//   PixInsight.exe -n --automation-mode -r="read_written_units.js,<units>,<originals>,<report>" --force-exit
//
// The report lists every unit with what was compared, every difference found and, last, how many units failed.

var unitsDirectory = jsArguments[0];
var sourcesDirectory = jsArguments[1];
var reportPath = jsArguments[2];

var report = "";
var failures = [];
var failed = false;

function note(text)
{
    report += text + "\n";
}

function fail(text)
{
    failed = true;
    note("  FAIL " + text);
}

// PixInsight's codes of the property types, as FileFormatInstance.imageProperties lists them.
var propertyTypes = {
    Boolean: 1, Int8: 2, Int16: 3, Int32: 4, Int64: 5, UInt8: 6, UInt16: 7, UInt32: 8, UInt64: 9, Float32: 10,
    Float64: 11, TimePoint: 14, I8Vector: 21, UI8Vector: 22, I16Vector: 23, UI16Vector: 24, I32Vector: 25,
    UI32Vector: 26, I64Vector: 27, UI64Vector: 28, F32Vector: 29, F64Vector: 30, I8Matrix: 33, UI8Matrix: 34,
    I16Matrix: 35, UI16Matrix: 36, I32Matrix: 37, UI32Matrix: 38, I64Matrix: 39, UI64Matrix: 40, F32Matrix: 41,
    F64Matrix: 42, String: 46
};
var isoStringType = 47;

// PixInsight's codes of the image types.
var imageTypes = ["Unknown", "Bias", "Dark", "Flat", "Light", "MasterBias", "MasterDark", "MasterFlat", "MasterLight",
                  "DefectMap", "RejectionMapHigh", "RejectionMapLow", "BinaryRejectionMapHigh",
                  "BinaryRejectionMapLow", "SlopeMap", "WeightMap"];

// The code of the written pattern at channel c of (x, y), as the test computes it.
function patternCode(c, x, y)
{
    return x * 3 + y * 5 + c * 4099 + (x * y) % 7;
}

// The sample of the written pattern in a format at channel c of (x, y): an integer format's code, or a floating point
// format's value.
function patternSample(format, c, x, y, width, height)
{
    var first = x == 0 && y == 0;
    var last = x + 1 == width && y + 1 == height;
    var code = patternCode(c, x, y);
    var narrow = (code * 11) % 65536;
    var wide = (code * 65537 + 12345) % 4294967296;
    switch (format)
    {
    case "UInt8":
        return first ? 0 : last ? 255 : code % 256;
    case "UInt16":
        return first ? 0 : last ? 65535 : narrow;
    case "UInt32":
        return first ? 0 : last ? 4294967295 : wide;
    case "Float32":
        return first ? 0 : last ? 1 : narrow / 65536;
    default:
        return first ? 0 : last ? 1 : wide / 4294967296;
    }
}

// The greatest code of an integer format, which PixInsight's normalized samples are scaled by; 0 for a float format.
function greatestCode(format)
{
    return format == "UInt8" ? 255 : format == "UInt16" ? 65535 : format == "UInt32" ? 4294967295 : 0;
}

// A unit opened through the XISF module: its description, image, keywords and properties.
function openUnit(path)
{
    var format = new FileFormat("XISF", true, false);
    var file = new FileFormatInstance(format);
    var descriptions = file.open(path, "");
    if (!descriptions || descriptions.length < 1)
        throw new Error("PixInsight does not open " + path);
    var d = descriptions[0];
    var image = new Image(1, 1, 1, 0, d.bitsPerSample, d.ieeefpSampleFormat ? 1 : 0);
    if (!file.readImage(image))
        throw new Error("PixInsight does not read the image of " + path);
    var properties = [];
    var listed = file.imageProperties;
    for (var i = 0; i < listed.length; ++i)
        properties.push({ id: listed[i][0], type: listed[i][1], value: file.readImageProperty(listed[i][0]) });
    var unit = {
        count: descriptions.length,
        description: d,
        image: image,
        keywords: file.keywords,
        properties: properties,
        cfa: file.colorFilterArray,
        displayFunction: file.displayFunction,
        rgbws: file.rgbws
    };
    file.close();
    return unit;
}

// The samples of one channel of an image, row after row.
function channelSamples(image, channel)
{
    var samples = new Float64Array(image.width * image.height);
    image.getSamples(samples, new Rect(0, 0, image.width, image.height), channel);
    return samples;
}

// The elements of a vector, byte array, matrix or array of a property value, in order.
function elementsOf(value)
{
    var elements = [];
    if (value instanceof Matrix)
    {
        for (var r = 0; r < value.rows; ++r)
            for (var c = 0; c < value.cols; ++c)
                elements.push(value.at(r, c));
        return elements;
    }
    if (value instanceof Array)
        return value;
    for (var i = 0; i < value.length; ++i)
        elements.push(value.at(i));
    return elements;
}

// A property value as text, for a report and for comparing two of them.
function textOf(value)
{
    if (value === null || value === undefined)
        return String(value);
    if (value instanceof Date)
        return value.toISOString();
    if (typeof value == "object")
    {
        var text = elementsOf(value).join(",");
        return value instanceof Matrix ? value.rows + "x" + value.cols + ":" + text : "[" + text + "]";
    }
    return typeof value == "string" ? JSON.stringify(value) : String(value);
}

// The milliseconds since the epoch of an ISO 8601 time in UTC, YYYY-MM-DDTHH:MM:SS[.fraction]Z.
function epochOf(text)
{
    var m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(\.\d+)?Z$/.exec(text);
    if (!m)
        return NaN;
    var milliseconds = m[7] ? Math.round(parseFloat("0" + m[7]) * 1000) : 0;
    return Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +m[6], milliseconds);
}

// Whether a property value is the value the catalogue states for a property of its type.
function sameAsStated(value, type, stated)
{
    if (type == "TimePoint")
        return value instanceof Date && value.getTime() == epochOf(stated);
    if (type.indexOf("Matrix") >= 0)
    {
        if (!(value instanceof Matrix) || value.rows != stated.rows || value.cols != stated.columns)
            return false;
        return elementsOf(value).join(",") == stated.elements.join(",");
    }
    if (type.indexOf("Vector") >= 0)
        return elementsOf(value).join(",") == stated.join(",");
    return value === stated;
}

// The type code PixInsight lists for a property of an XISF type: a String is a String or an IsoString.
function typeMatches(code, type)
{
    return code == propertyTypes[type] || (type == "String" && code == isoStringType);
}

function compareGeometry(entry, read)
{
    var d = read.description;
    if (d.width != entry.width || d.height != entry.height || d.numberOfChannels != entry.channels)
        fail("geometry " + d.width + "x" + d.height + "x" + d.numberOfChannels + ", written " + entry.width + "x" +
             entry.height + "x" + entry.channels);
    var bits = { UInt8: 8, UInt16: 16, UInt32: 32, Float32: 32, Float64: 64 }[entry.format];
    var real = entry.format.indexOf("Float") == 0;
    if (d.bitsPerSample != bits || d.ieeefpSampleFormat != real)
        fail("sample format " + d.bitsPerSample + " bits" + (d.ieeefpSampleFormat ? " float" : " integer") +
             ", written " + entry.format);
    var space = entry.colorSpace == "Gray" ? 0 : entry.colorSpace == "RGB" ? 1 : -1;
    if (d.colorSpace != space)
        fail("colour space " + d.colorSpace + ", written " + entry.colorSpace);
    if (entry.imageType !== null && imageTypes[d.imageType] != entry.imageType)
        fail("image type " + imageTypes[d.imageType] + " (" + d.imageType + "), written " + entry.imageType);
    if (read.count != 1)
        fail(read.count + " images, written 1");
}

// Every sample of the written pattern.
function comparePattern(entry, read)
{
    var greatest = greatestCode(entry.format);
    var differences = 0;
    for (var c = 0; c < entry.channels; ++c)
    {
        var samples = channelSamples(read.image, c);
        for (var y = 0; y < entry.height; ++y)
            for (var x = 0; x < entry.width; ++x)
            {
                var value = samples[x + y * entry.width];
                var stated = patternSample(entry.format, c, x, y, entry.width, entry.height);
                // An integer sample is normalized by PixInsight: it is the code it was written as when scaling it back
                // gives that code to well within one.
                var same = greatest > 0 ? Math.abs(value * greatest - stated) < 1e-6 : value === stated;
                if (!same && ++differences <= 3)
                    fail("channel " + c + " at (" + x + ", " + y + ") reads " + value +
                         (greatest > 0 ? " = code " + value * greatest : "") + ", written " + stated);
            }
    }
    if (differences > 0)
        fail(differences + " samples differ");
    else
        note("  samples: all " + entry.width * entry.height * entry.channels + " as written");
}

// Every sample of PixInsight's reading of the original.
function compareSourceSamples(entry, read, original)
{
    var differences = 0;
    for (var c = 0; c < entry.channels; ++c)
    {
        var a = channelSamples(read.image, c);
        var b = channelSamples(original.image, c);
        for (var i = 0; i < a.length; ++i)
            if (a[i] !== b[i] && ++differences <= 3)
                fail("channel " + c + " sample " + i + " reads " + a[i] + ", the original " + b[i]);
    }
    if (differences > 0)
        fail(differences + " samples differ from the original");
    else
        note("  samples: all " + entry.width * entry.height * entry.channels + " as the original reads");
}

function keywordText(k)
{
    return k.name + " = [" + k.value + "] / [" + k.comment + "]";
}

// The keywords the catalogue lists, each in the order written; any other keyword read is reported.
function compareStatedKeywords(entry, read)
{
    var used = [];
    for (var i = 0; i < entry.keywords.length; ++i)
    {
        var stated = entry.keywords[i];
        var found = -1;
        for (var k = 0; k < read.keywords.length && found < 0; ++k)
            if (!used[k] && read.keywords[k].name == stated.name)
                found = k;
        if (found < 0)
        {
            fail("keyword " + stated.name + " is not read");
            continue;
        }
        used[found] = true;
        var k = read.keywords[found];
        if (k.value != stated.value || k.comment != stated.comment)
            fail("keyword " + keywordText(k) + ", written " + keywordText(stated));
    }
    for (var k = 0; k < read.keywords.length; ++k)
        if (!used[k])
            note("  keyword not in the catalogue: " + keywordText(read.keywords[k]));
    note("  keywords: " + entry.keywords.length + " stated, " + read.keywords.length + " read");
}

// The properties the catalogue lists, each with its type and value; any other property read is reported.
function compareStatedProperties(entry, read)
{
    var byId = {};
    for (var i = 0; i < read.properties.length; ++i)
        byId[read.properties[i].id] = read.properties[i];
    var listed = {};
    for (var i = 0; i < entry.properties.length; ++i)
    {
        var stated = entry.properties[i];
        listed[stated.id] = true;
        var p = byId[stated.id];
        if (!p)
        {
            fail("property " + stated.id + " is not read");
            continue;
        }
        if (!typeMatches(p.type, stated.type))
            fail("property " + stated.id + " reads as type " + p.type + ", written " + stated.type);
        if (!sameAsStated(p.value, stated.type, stated.value))
            fail("property " + stated.id + " reads " + textOf(p.value) + ", written " + JSON.stringify(stated.value));
    }
    for (var i = 0; i < read.properties.length; ++i)
        if (!listed[read.properties[i].id])
            note("  property not in the catalogue: " + read.properties[i].id + " (" + read.properties[i].type + ") = " +
                 textOf(read.properties[i].value).substring(0, 120));
    note("  properties: " + entry.properties.length + " stated, " + read.properties.length + " read");
}

// The keywords of the original, in their order and each the same. The writer also states the acquisition facts it
// read in its own spelling - EXPOSURE beside EXPTIME - as it does in a FITS file; a keyword the original does not
// have is reported.
function compareSourceKeywords(read, original)
{
    var a = read.keywords.map(keywordText);
    var b = original.keywords.map(keywordText);
    var at = 0;
    for (var i = 0; i < a.length; ++i)
        if (at < b.length && a[i] == b[at])
            ++at;
        else
            note("  keyword the original does not have: " + a[i]);
    if (at < b.length)
        fail("keyword " + b[at] + " of the original is not read, in its place");
    note("  keywords: " + a.length + " read, the original " + b.length);
}

// The properties of the original, each with its type and value; a property only one of them has is reported.
function compareSourceProperties(read, original)
{
    var byId = {};
    for (var i = 0; i < read.properties.length; ++i)
        byId[read.properties[i].id] = read.properties[i];
    var seen = {};
    for (var i = 0; i < original.properties.length; ++i)
    {
        var o = original.properties[i];
        seen[o.id] = true;
        var p = byId[o.id];
        if (!p)
        {
            fail("property " + o.id + " of the original is not read");
            continue;
        }
        // XISF has one String type, which PixInsight lists as a String or, when its text was held in a data block, an
        // IsoString.
        var string = function (code) { return code == propertyTypes.String || code == isoStringType; };
        if (p.type != o.type && !(string(p.type) && string(o.type)))
            fail("property " + o.id + " reads as type " + p.type + ", the original " + o.type);
        if (textOf(p.value) != textOf(o.value))
            fail("property " + o.id + " reads " + textOf(p.value).substring(0, 200) + ", the original " +
                 textOf(o.value).substring(0, 200));
    }
    for (var i = 0; i < read.properties.length; ++i)
        if (!seen[read.properties[i].id])
            note("  property the original does not have: " + read.properties[i].id + " (" + read.properties[i].type +
                 ") = " + textOf(read.properties[i].value).substring(0, 120));
    note("  properties: " + read.properties.length + " read, the original " + original.properties.length);
}

function compareFilterArray(entry, read)
{
    var cfa = read.cfa;
    if (entry.cfa === null)
    {
        if (cfa && cfa[0] != "")
            fail("filter array " + JSON.stringify(cfa) + ", none written");
        return;
    }
    if (!cfa || cfa[0] != entry.cfa.pattern || cfa[1] != entry.cfa.width || cfa[2] != entry.cfa.height ||
        cfa[3] != entry.cfa.name)
        fail("filter array " + JSON.stringify(cfa) + ", written " + JSON.stringify(entry.cfa));
    else
        note("  filter array: " + JSON.stringify(cfa));
}

function compareStructure(entry, read)
{
    var s = entry.structure;
    if (s.id !== undefined)
    {
        var id = null;
        for (var i = 0; i < read.properties.length; ++i)
            if (read.properties[i].id == "Image:Id")
                id = read.properties[i].value;
        if (id != s.id)
            fail("identifier " + id + ", written " + s.id);
    }
    if (s.resolution !== undefined)
    {
        var d = read.description;
        if (d.xResolution != s.resolution[0] || d.yResolution != s.resolution[1] ||
            d.metricResolution != (s.resolution[2] == "cm"))
            fail("resolution " + d.xResolution + ";" + d.yResolution + (d.metricResolution ? " cm" : " inch") +
                 ", written " + s.resolution.join(";"));
        else
            note("  resolution: " + s.resolution.join(";"));
    }
    if (s.displayFunction !== undefined)
    {
        // The catalogue states the five parameters - midtones, shadows, highlights, shadows and highlights expansion -
        // of four channels each, as XISF writes them; PixInsight lists the four channels of five parameters each.
        var f = read.displayFunction;
        var read5 = [];
        for (var c = 0; f && c < f.length; ++c)
            read5.push(elementsOf(f[c]).join(","));
        var stated5 = [];
        for (var c = 0; c < 4; ++c)
        {
            var channel = [];
            for (var p = 0; p < s.displayFunction.length; ++p)
                channel.push(s.displayFunction[p][c]);
            stated5.push(channel.join(","));
        }
        if (read5.join(";") != stated5.join(";"))
            fail("display function " + read5.join(";") + ", written " + stated5.join(";"));
        else
            note("  display function: " + read5.join(";"));
    }
}

// The angle between two directions on the sky, in degrees.
function separation(ra1, dec1, ra2, dec2)
{
    var k = Math.PI / 180;
    var a = Math.sin((dec2 - dec1) * k / 2);
    var b = Math.sin((ra2 - ra1) * k / 2);
    var h = a * a + Math.cos(dec1 * k) * Math.cos(dec2 * k) * b * b;
    return 2 * Math.asin(Math.min(1, Math.sqrt(h))) / k;
}

// The grid of points the catalogue lists, through the solution PixInsight reads, both ways: where it puts each pixel on
// the sky, and where it puts each direction in the image, each within a hundredth of a pixel of the engine's.
//
// PixInsight 1.9.6 builds a solution from the world-coordinate keywords out of their linear part alone: it leaves the
// SIP polynomials out (A, B, AP and BP), with or without an inverse model. A solution with a distortion is therefore
// held, in that case, to where the engine's linear part alone puts each point, and the report says PixInsight left the
// distortion out; a solution that matches neither fails.
function compareStatedSky(entry, path)
{
    var window = ImageWindow.open(path)[0];
    try
    {
        if (!window.hasAstrometricSolution)
        {
            fail("PixInsight reads no astrometric solution");
            return;
        }
        var within = function (ra, dec)
        {
            var forward = 0;
            var inverse = 0;
            for (var i = 0; i < entry.sky.length; ++i)
            {
                var point = entry.sky[i];
                var sky = window.imageToCelestial(point[0], point[1]);
                forward = Math.max(forward, separation(sky.x, sky.y, point[ra], point[dec]) / entry.scale);
                var at = window.celestialToImage(point[ra], point[dec]);
                inverse = Math.max(inverse, Math.sqrt((at.x - point[0]) * (at.x - point[0]) +
                                                      (at.y - point[1]) * (at.y - point[1])));
            }
            return { forward: forward, inverse: inverse, holds: forward <= 0.01 && inverse <= 0.01 };
        };
        var whole = within(2, 3);
        var text = function (found)
        {
            return "image to sky within " + found.forward.toExponential(2) + " px and sky to image within " +
                   found.inverse.toExponential(2) + " px";
        };
        if (whole.holds)
        {
            note("  solution: " + entry.sky.length + " points, " + text(whole) + " of the engine's");
            return;
        }
        var linear = entry.distortion ? within(4, 5) : null;
        if (linear && linear.holds)
            note("  solution: PixInsight leaves the SIP distortion out, " + text(whole) + " of the engine's whole " +
                 "solution; " + entry.sky.length + " points, " + text(linear) + " of its linear part");
        else
            fail("the solution is " + text(whole) + " of the engine's" +
                 (linear ? ", and " + text(linear) + " of its linear part" : ""));
    }
    finally
    {
        window.forceClose();
    }
}

// The solution of the original against the unit's, over a grid of the image and beyond it.
function compareSourceSky(path, originalPath, width, height)
{
    var a = ImageWindow.open(path)[0];
    var b = ImageWindow.open(originalPath)[0];
    try
    {
        if (!b.hasAstrometricSolution)
            return;
        if (!a.hasAstrometricSolution)
        {
            fail("PixInsight reads no astrometric solution, and reads one in the original");
            return;
        }
        var largest = 0;
        var count = 0;
        for (var fx = -0.25; fx <= 1.25; fx += 0.125)
            for (var fy = -0.25; fy <= 1.25; fy += 0.125)
            {
                var p = a.imageToCelestial(fx * width, fy * height);
                var q = b.imageToCelestial(fx * width, fy * height);
                var d = Math.max(Math.abs(p.x - q.x), Math.abs(p.y - q.y));
                largest = Math.max(largest, d);
                ++count;
            }
        if (largest > 0)
            fail("the solution differs from the original's by up to " + largest + " degrees");
        else
            note("  solution: " + count + " points where the original puts them");
    }
    finally
    {
        a.forceClose();
        b.forceClose();
    }
}

try
{
    note("PixInsight " + CoreApplication.versionMajor + "." + CoreApplication.versionMinor + "." +
         CoreApplication.versionRelease + " build " + CoreApplication.versionBuild + ", " + CoreApplication.platform);
    var catalogue = JSON.parse(File.readTextFile(unitsDirectory + "/units.json"));
    note("Units written by AfterNightEngine " + catalogue.engine + " through OpenXISF " + catalogue.openxisf);
    for (var u = 0; u < catalogue.units.length; ++u)
    {
        var entry = catalogue.units[u];
        var path = unitsDirectory + "/" + entry.file;
        failed = false;
        note("");
        note(entry.file + ": " + entry.format + " " + entry.colorSpace + " " + entry.storage + ", " +
             (entry.block.codec ? entry.block.codec + (entry.block.shuffle ? "+sh" : "") + " in " +
              entry.block.subblocks + " subblocks" : "uncompressed") +
             (entry.block.checksum ? ", " + entry.block.checksum : "") + (entry.reserved ? ", reserved" : ""));
        try
        {
            var read = openUnit(path);
            compareGeometry(entry, read);
            if (entry.pixels == "source")
            {
                var original = openUnit(sourcesDirectory + "/" + entry.source);
                compareSourceSamples(entry, read, original);
                compareSourceKeywords(read, original);
                compareSourceProperties(read, original);
                compareFilterArray({ cfa: original.cfa && original.cfa[0] != "" ?
                                     { pattern: original.cfa[0], width: original.cfa[1], height: original.cfa[2],
                                       name: original.cfa[3] } : null }, read);
                compareSourceSky(path, sourcesDirectory + "/" + entry.source, entry.width, entry.height);
            }
            else
            {
                comparePattern(entry, read);
                compareStatedKeywords(entry, read);
                compareStatedProperties(entry, read);
                compareFilterArray(entry, read);
                compareStructure(entry, read);
                if (entry.sky.length > 0)
                    compareStatedSky(entry, path);
            }
        }
        catch (error)
        {
            fail(String(error));
        }
        if (failed)
            failures.push(entry.file);
        note(failed ? "  FAILED" : "  ok");
    }
    note("");
    note(catalogue.units.length + " units, " + failures.length + " failed" +
         (failures.length > 0 ? ": " + failures.join(", ") : ""));
}
catch (error)
{
    note("ERROR " + error);
}
File.writeTextFile(reportPath, report);

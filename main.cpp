#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")

// ============================================================
// SETTINGS
// ============================================================

const int SAMPLE_RATE = 44100;
const double PI = 3.14159265358979323846;

const double MIN_TEMPO = 40.0;
const double MAX_TEMPO = 480.0;
const double TEMPO_STEP = 5.0;

const int MIN_PITCH = -12;
const int MAX_PITCH = 12;
const int PITCH_STEP = 1;

const double MASTER_GAIN = 2.5;

// ============================================================
// FFMPEG
// ============================================================

// Change this if ffmpeg.exe is stored somewhere else.
//
// Example:
// const char* FFMPEG_PATH =
//     "C:\\ffmpeg\\bin\\ffmpeg.exe";

const char* FFMPEG_PATH = "C:\\ffmpeg\\ffmpeg-9.0.1-essentials_build\\bin\\ffmpeg.exe";

// ============================================================
// COLORS
// ============================================================

const COLORREF BACKGROUND = RGB(0, 207, 74);
const COLORREF COLUMN = RGB(32, 32, 38);
const COLORREF COLUMN_BORDER = RGB(55, 55, 65);
const COLORREF TEXT_COLOR = RGB(255, 255, 255);
const COLORREF BUTTON_TEXT = RGB(0, 255, 0);
const COLORREF EDIT_BACKGROUND = RGB(15, 15, 20);
const COLORREF EDIT_TEXT = RGB(255, 255, 255);

// ============================================================
// MAIN WINDOW
// ============================================================

HWND mainWindow = nullptr;

HWND playButton = nullptr;
HWND playAllButton = nullptr;
HWND loopButton = nullptr;
HWND boostButton = nullptr;

HWND instrumentsButton = nullptr;
HWND chordsButton = nullptr;
HWND drumsButton = nullptr;

HWND tempoMinusButton = nullptr;
HWND tempoPlusButton = nullptr;
HWND tempoLabel = nullptr;

HWND pitchMinusButton = nullptr;
HWND pitchPlusButton = nullptr;
HWND pitchLabel = nullptr;

HWND saveTrackButton = nullptr;
HWND combineSaveButton = nullptr;
HWND importButton = nullptr;

HWND exportWavButton = nullptr;
HWND exportMp3Button = nullptr;
HWND exportMp4Button = nullptr;

HWND songEditor = nullptr;

HBRUSH editBrush = nullptr;

// ============================================================
// PLAYER STATE
// ============================================================

std::atomic_bool playing(false);
std::atomic_bool looping(false);
std::atomic_bool stopRequested(false);
std::atomic_bool playingAll(false);

std::atomic<double> currentTempo(120.0);
std::atomic<int> currentPitch(0);
std::atomic<int> volumeBoost(0);

// ============================================================
// LAYOUT
// ============================================================

const int LEFT_MARGIN = 15;
const int EDITOR_TOP = 205;
const int EDITOR_HEIGHT = 350;

const int TRACK_TOP = 580;
const int TRACK_BUTTON_HEIGHT = 36;
const int TRACK_GAP = 8;

int scrollY = 0;
int contentHeight = 900;

// ============================================================
// SAVED TRACKS
// ============================================================

std::vector<std::string> savedTracks;

// ============================================================
// COMMAND IDS
// ============================================================

const UINT ID_PLAY = 1000;
const UINT ID_PLAY_ALL = 1011;
const UINT ID_LOOP = 1001;
const UINT ID_BOOST = 1002;

const UINT ID_INSTRUMENTS = 1003;
const UINT ID_CHORDS = 1004;
const UINT ID_DRUMS = 1005;

const UINT ID_TEMPO_MINUS = 1006;
const UINT ID_TEMPO_PLUS = 1007;

const UINT ID_PITCH_MINUS = 1008;
const UINT ID_PITCH_PLUS = 1009;

const UINT ID_SAVE_TRACK = 1010;
const UINT ID_COMBINE_SAVE = 1012;
const UINT ID_IMPORT_TRACKS = 1013;

const UINT ID_EXPORT_WAV = 1014;
const UINT ID_EXPORT_MP3 = 1015;
const UINT ID_EXPORT_MP4 = 1016;

const UINT ID_LOAD_TRACK_BASE = 3000;
const UINT ID_PLAY_TRACK_BASE = 4000;
const UINT ID_DELETE_TRACK_BASE = 5000;

const UINT ID_NOTE_BASE = 10000;
const UINT ID_CHORD_BASE = 20000;
const UINT ID_DRUM_BASE = 30000;

std::map<UINT, std::string> noteCommands;
std::map<UINT, std::string> chordCommands;
std::map<UINT, std::string> drumCommands;

// ============================================================
// SONG DATA
// ============================================================

struct NoteEvent
{
    double startBeat;
    double durationBeats;
    double frequency;
    std::string instrument;
};

struct DrumEvent
{
    double startBeat;
    std::string type;
};

// ============================================================
// RANDOM NOISE
// ============================================================

double noise()
{
    thread_local std::mt19937 generator(
        std::random_device{}()
    );

    thread_local std::uniform_real_distribution<double>
        distribution(-1.0, 1.0);

    return distribution(generator);
}

// ============================================================
// UPPERCASE
// ============================================================

std::string upper(std::string value)
{
    for (char& c : value)
    {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }

    return value;
}

// ============================================================
// NOTE FREQUENCY
// ============================================================

double noteFrequency(const std::string& note)
{
    if (note.size() < 2)
        return 0.0;

    char letter = note[0];

    int semitoneFromC = -1;

    switch (letter)
    {
        case 'C': semitoneFromC = 0; break;
        case 'D': semitoneFromC = 2; break;
        case 'E': semitoneFromC = 4; break;
        case 'F': semitoneFromC = 5; break;
        case 'G': semitoneFromC = 7; break;
        case 'A': semitoneFromC = 9; break;
        case 'B': semitoneFromC = 11; break;
        default: return 0.0;
    }

    size_t position = 1;

    if (
        position < note.size() &&
        note[position] == '#')
    {
        semitoneFromC++;
        position++;
    }

    if (position >= note.size())
        return 0.0;

    int octave = 0;

    try
    {
        octave = std::stoi(
            note.substr(position)
        );
    }
    catch (...)
    {
        return 0.0;
    }

    if (semitoneFromC >= 12)
    {
        semitoneFromC -= 12;
        octave++;
    }

    int midiNote =
        (octave + 1) * 12 +
        semitoneFromC;

    if (midiNote < 21 || midiNote > 108)
        return 0.0;

    return 440.0 *
        std::pow(
            2.0,
            (midiNote - 69) / 12.0
        );
}

// ============================================================
// PIANO NOTES
// ============================================================

std::vector<std::string> GetNotes()
{
    std::vector<std::string> notes;

    const char* names[] =
    {
        "C","C#","D","D#","E","F",
        "F#","G","G#","A","A#","B"
    };

    for (int midi = 21; midi <= 108; ++midi)
    {
        int octave = (midi / 12) - 1;
        int index = midi % 12;

        notes.push_back(
            std::string(names[index]) +
            std::to_string(octave)
        );
    }

    return notes;
}

// ============================================================
// INSTRUMENTS
// ============================================================

std::vector<std::string> GetInstruments()
{
    std::vector<std::string> instruments =
    {
        "BASS",
        "BELL",
        "FLUTE",
        "GUITAR",
        "ORGAN",
        "PIANO",
        "SYNTH",
        "TRUMPET"
    };

    std::sort(
        instruments.begin(),
        instruments.end()
    );

    return instruments;
}

// ============================================================
// INSTRUMENT WAVEFORM
// ============================================================

double instrumentWave(
    const std::string& instrument,
    double frequency,
    double t)
{
    double phase =
        2.0 * PI * frequency * t;

    if (instrument == "PIANO")
    {
        double envelope =
            std::exp(-1.7 * t);

        return
            (
                std::sin(phase) +
                std::sin(phase * 2.0) * 0.35 +
                std::sin(phase * 3.0) * 0.15 +
                std::sin(phase * 4.0) * 0.06
            ) * envelope;
    }

    if (instrument == "BASS")
    {
        double envelope =
            std::exp(-1.0 * t);

        return
            (
                std::sin(phase) * 0.9 +
                std::sin(phase * 2.0) * 0.25 +
                std::sin(phase * 3.0) * 0.08
            ) * envelope;
    }

    if (instrument == "GUITAR")
    {
        double attack =
            1.0 - std::exp(-120.0 * t);

        double decay =
            std::exp(-3.8 * t);

        double pluck =
            std::exp(-35.0 * t);

        double sound =
            std::sin(phase) * 0.60 +
            std::sin(phase * 2.0) * 0.30 +
            std::sin(phase * 3.0) * 0.18 +
            std::sin(phase * 5.0) * 0.08;

        return
            sound * attack * decay +
            std::sin(phase * 2.0) *
            pluck * 0.12;
    }

    if (instrument == "SYNTH")
    {
        double saw =
            2.0 *
            (
                frequency * t -
                std::floor(
                    frequency * t + 0.5
                )
            );

        return saw * 0.8;
    }

    if (instrument == "ORGAN")
    {
        return
            std::sin(phase) * 0.65 +
            std::sin(phase * 2.0) * 0.25 +
            std::sin(phase * 4.0) * 0.12 +
            std::sin(phase * 8.0) * 0.04;
    }

    if (instrument == "FLUTE")
    {
        return
            (
                std::sin(phase) * 0.85 +
                std::sin(phase * 2.0) * 0.10
            ) *
            std::exp(-0.8 * t);
    }

    if (instrument == "TRUMPET")
    {
        double attack =
            1.0 - std::exp(-45.0 * t);

        double envelope =
            0.85 +
            0.15 * std::exp(-1.0 * t);

        double brass =
            std::sin(phase) * 0.45 +
            std::sin(phase * 2.0) * 0.30 +
            std::sin(phase * 3.0) * 0.28 +
            std::sin(phase * 4.0) * 0.20 +
            std::sin(phase * 5.0) * 0.14 +
            std::sin(phase * 6.0) * 0.08;

        return brass * attack * envelope;
    }

    if (instrument == "BELL")
    {
        double envelope =
            std::exp(-2.2 * t);

        return
            (
                std::sin(phase) +
                std::sin(phase * 2.71) * 0.4 +
                std::sin(phase * 4.13) * 0.2
            ) * envelope;
    }

    return std::sin(phase);
}

// ============================================================
// DRUMS
// ============================================================

double kick(double t)
{
    if (t < 0.0 || t >= 0.8)
        return 0.0;

    double frequency =
        155.0 * std::exp(-22.0 * t) + 48.0;

    double body =
        std::sin(
            2.0 * PI * frequency * t
        ) * std::exp(-6.0 * t);

    double beater =
        std::sin(
            2.0 * PI * 95.0 * t
        ) * std::exp(-55.0 * t);

    double click =
        noise() * std::exp(-140.0 * t);

    return
        body * 1.20 +
        beater * 0.28 +
        click * 0.025;
}

double snare(double t)
{
    if (t < 0.0 || t >= 0.5)
        return 0.0;

    double wire =
        noise() * std::exp(-17.0 * t);

    double body =
        std::sin(
            2.0 * PI * 190.0 * t
        ) * std::exp(-19.0 * t);

    double ring =
        std::sin(
            2.0 * PI * 330.0 * t
        ) * std::exp(-27.0 * t);

    double attack =
        std::sin(
            2.0 * PI * 1700.0 * t
        ) * std::exp(-105.0 * t);

    return
        wire * 0.68 +
        body * 0.48 +
        ring * 0.16 +
        attack * 0.14;
}

double closedHiHat(double t)
{
    if (t < 0.0 || t >= 0.18)
        return 0.0;

    double metal =
        noise() * 0.72 +
        std::sin(2.0 * PI * 6800.0 * t) * 0.14 +
        std::sin(2.0 * PI * 8900.0 * t) * 0.10 +
        std::sin(2.0 * PI * 11200.0 * t) * 0.06;

    return
        metal *
        std::exp(-38.0 * t) *
        0.55;
}

double openHiHat(double t)
{
    if (t < 0.0 || t >= 0.8)
        return 0.0;

    double metal =
        noise() * 0.72 +
        std::sin(2.0 * PI * 6200.0 * t) * 0.14 +
        std::sin(2.0 * PI * 8400.0 * t) * 0.10 +
        std::sin(2.0 * PI * 10500.0 * t) * 0.06;

    double attack =
        1.0 - std::exp(-120.0 * t);

    return
        metal *
        attack *
        std::exp(-5.0 * t) *
        0.45;
}

double tom(double t, double frequency)
{
    if (t < 0.0 || t >= 0.9)
        return 0.0;

    double pitch =
        frequency *
        (
            1.0 +
            0.15 * std::exp(-8.0 * t)
        );

    double body =
        std::sin(
            2.0 * PI * pitch * t
        );

    double harmonic =
        std::sin(
            2.0 * PI * pitch * 1.98 * t
        ) * 0.16;

    double attack =
        std::sin(
            2.0 * PI * 700.0 * t
        ) * std::exp(-45.0 * t);

    return
        (
            body +
            harmonic +
            attack * 0.08
        ) *
        std::exp(-5.0 * t) *
        0.75;
}

double crash(double t)
{
    if (t < 0.0 || t >= 2.5)
        return 0.0;

    double metal =
        noise() * 0.72 +
        std::sin(2.0 * PI * 3900.0 * t) * 0.10 +
        std::sin(2.0 * PI * 5100.0 * t) * 0.08 +
        std::sin(2.0 * PI * 7300.0 * t) * 0.06;

    return
        metal *
        (1.0 - std::exp(-180.0 * t)) *
        std::exp(-1.7 * t) *
        0.65;
}

double ride(double t)
{
    if (t < 0.0 || t >= 1.5)
        return 0.0;

    double metallicNoise =
        noise() * 0.32;

    double bell =
        std::sin(
            2.0 * PI * 2800.0 * t
        ) * 0.22;

    double metallicTone =
        std::sin(
            2.0 * PI * 4700.0 * t
        ) * 0.15;

    return
        (
            metallicNoise +
            bell +
            metallicTone
        ) *
        std::exp(-2.7 * t) *
        0.45;
}

double rimshot(double t)
{
    if (t < 0.0 || t >= 0.18)
        return 0.0;

    double attack =
        std::sin(
            2.0 * PI * 1250.0 * t
        ) * std::exp(-45.0 * t);

    double wood =
        std::sin(
            2.0 * PI * 420.0 * t
        ) * std::exp(-35.0 * t);

    double click =
        noise() * std::exp(-75.0 * t);

    return
        attack * 0.65 +
        wood * 0.30 +
        click * 0.20;
}

double makeDrum(
    const std::string& type,
    double t)
{
    if (
        type == "KICK" ||
        type == "BASS_DRUM")
        return kick(t);

    if (type == "SNARE")
        return snare(t);

    if (
        type == "HIHAT" ||
        type == "CLOSED_HIHAT")
        return closedHiHat(t);

    if (
        type == "OPEN_HIHAT" ||
        type == "OPENHIHAT")
        return openHiHat(t);

    if (type == "LOW_TOM")
        return tom(t, 95.0);

    if (type == "MID_TOM")
        return tom(t, 145.0);

    if (type == "HIGH_TOM")
        return tom(t, 210.0);

    if (type == "CRASH")
        return crash(t);

    if (type == "RIDE")
        return ride(t);

    if (type == "RIMSHOT")
        return rimshot(t);

    return 0.0;
}

// ============================================================
// LOAD SONG TEXT
// ============================================================

bool LoadSongText(
    const std::string& text,
    std::vector<NoteEvent>& notes,
    std::vector<DrumEvent>& drums,
    double& tempo,
    double& loopLengthBeats)
{
    notes.clear();
    drums.clear();

    tempo = 120.0;
    loopLengthBeats = 4.0;

    std::istringstream input(text);
    std::string command;

    while (input >> command)
    {
        command = upper(command);

        if (command == "TEMPO")
        {
            input >> tempo;

            tempo =
                std::max(
                    MIN_TEMPO,
                    std::min(MAX_TEMPO, tempo)
                );
        }
        else if (command == "LENGTH")
        {
            input >> loopLengthBeats;

            if (loopLengthBeats <= 0)
                loopLengthBeats = 4.0;
        }
        else if (
            command == "NOTE" ||
            command == "PIANO" ||
            command == "BASS" ||
            command == "GUITAR" ||
            command == "SYNTH" ||
            command == "ORGAN" ||
            command == "FLUTE" ||
            command == "TRUMPET" ||
            command == "BELL")
        {
            std::string noteName;
            double startBeat = 0.0;
            double durationBeats = 1.0;

            input >>
                noteName >>
                startBeat >>
                durationBeats;

            double frequency =
                noteFrequency(
                    upper(noteName)
                );

            if (frequency > 0.0)
            {
                std::string instrument = command;

                if (command == "NOTE")
                    instrument = "PIANO";

                notes.push_back(
                    {
                        startBeat,
                        durationBeats,
                        frequency,
                        instrument
                    }
                );
            }
        }
        else if (command == "DRUM")
        {
            std::string type;
            double startBeat = 0.0;

            input >>
                type >>
                startBeat;

            drums.push_back(
                {
                    startBeat,
                    upper(type)
                }
            );
        }
    }

    return true;
}

// ============================================================
// EDITOR TEXT
// ============================================================

std::string GetEditorText()
{
    if (!songEditor)
        return "";

    int length =
        GetWindowTextLengthA(songEditor);

    if (length <= 0)
        return "";

    std::string text(
        length + 1,
        '\0'
    );

    int actualLength =
        GetWindowTextA(
            songEditor,
            &text[0],
            length + 1
        );

    text.resize(actualLength);

    return text;
}

void SetEditorText(
    const std::string& text)
{
    if (!songEditor)
        return;

    SetWindowTextA(
        songEditor,
        text.c_str()
    );
}

void InsertEditorText(
    const std::string& text)
{
    if (!songEditor)
        return;

    SendMessageA(
        songEditor,
        EM_REPLACESEL,
        TRUE,
        reinterpret_cast<LPARAM>(
            text.c_str()
        )
    );
}

// ============================================================
// VOLUME
// ============================================================

double GetVolumeMultiplier(int level)
{
    switch (level)
    {
        case 1: return 2.0;
        case 2: return 3.5;
        case 3: return 5.0;
        default: return 1.0;
    }
}

// ============================================================
// GENERATE AUDIO
// ============================================================

bool GenerateAudio(
    const std::string& text,
    double tempoOverride,
    int pitchSemitones,
    double volumeMultiplier,
    std::vector<short>& samples)
{
    std::vector<NoteEvent> notes;
    std::vector<DrumEvent> drums;

    double fileTempo;
    double loopLengthBeats;

    if (!LoadSongText(
        text,
        notes,
        drums,
        fileTempo,
        loopLengthBeats))
    {
        return false;
    }

    double tempo =
        std::max(
            MIN_TEMPO,
            std::min(MAX_TEMPO, tempoOverride)
        );

    int pitch =
        std::max(
            MIN_PITCH,
            std::min(MAX_PITCH, pitchSemitones)
        );

    double pitchMultiplier =
        std::pow(
            2.0,
            static_cast<double>(pitch) / 12.0
        );

    double secondsPerBeat =
        60.0 / tempo;

    double loopDuration =
        loopLengthBeats *
        secondsPerBeat;

    int totalSamples =
        static_cast<int>(
            std::round(
                loopDuration *
                SAMPLE_RATE
            )
        );

    if (totalSamples <= 0)
        return false;

    std::vector<double> audio(
        totalSamples,
        0.0
    );

    // --------------------------------------------------------
    // NOTES
    // --------------------------------------------------------

    for (const NoteEvent& note : notes)
    {
        double startSeconds =
            note.startBeat *
            secondsPerBeat;

        double durationSeconds =
            note.durationBeats *
            secondsPerBeat;

        int startSample =
            static_cast<int>(
                std::round(
                    startSeconds *
                    SAMPLE_RATE
                )
            );

        int noteSamples =
            static_cast<int>(
                std::round(
                    durationSeconds *
                    SAMPLE_RATE
                )
            );

        double shiftedFrequency =
            note.frequency *
            pitchMultiplier;

        for (int i = 0;
             i < noteSamples;
             ++i)
        {
            int index =
                startSample + i;

            if (index < 0)
                continue;

            if (index >= totalSamples)
                break;

            double time =
                static_cast<double>(i) /
                SAMPLE_RATE;

            double envelope = 1.0;

            if (time < 0.01)
                envelope =
                    time / 0.01;

            double remaining =
                durationSeconds - time;

            if (remaining < 0.05)
            {
                envelope =
                    std::min(
                        envelope,
                        std::max(
                            0.0,
                            remaining / 0.05
                        )
                    );
            }

            audio[index] +=
                instrumentWave(
                    note.instrument,
                    shiftedFrequency,
                    time
                ) *
                envelope *
                0.55;
        }
    }

    // --------------------------------------------------------
    // DRUMS
    // --------------------------------------------------------

    for (const DrumEvent& drum : drums)
    {
        double startSeconds =
            drum.startBeat *
            secondsPerBeat;

        int startSample =
            static_cast<int>(
                std::round(
                    startSeconds *
                    SAMPLE_RATE
                )
            );

        int drumSamples =
            static_cast<int>(
                2.5 *
                SAMPLE_RATE
            );

        for (int i = 0;
             i < drumSamples;
             ++i)
        {
            int index =
                startSample + i;

            if (index < 0)
                continue;

            if (index >= totalSamples)
                break;

            double time =
                static_cast<double>(i) /
                SAMPLE_RATE;

            double drumVolume = 1.0;

            if (
                drum.type == "KICK" ||
                drum.type == "BASS_DRUM")
                drumVolume = 2.25;
            else if (drum.type == "SNARE")
                drumVolume = 1.70;
            else if (
                drum.type == "HIHAT" ||
                drum.type == "CLOSED_HIHAT")
                drumVolume = 0.85;
            else if (drum.type == "OPEN_HIHAT")
                drumVolume = 0.95;
            else if (
                drum.type == "LOW_TOM" ||
                drum.type == "MID_TOM" ||
                drum.type == "HIGH_TOM")
                drumVolume = 1.15;
            else if (drum.type == "CRASH")
                drumVolume = 1.05;
            else if (drum.type == "RIDE")
                drumVolume = 0.85;
            else if (drum.type == "RIMSHOT")
                drumVolume = 0.90;

            audio[index] +=
                makeDrum(
                    drum.type,
                    time
                ) *
                drumVolume;
        }
    }

    // --------------------------------------------------------
    // MASTER
    // --------------------------------------------------------

    if (volumeMultiplier < 1.0)
        volumeMultiplier = 1.0;

    samples.resize(totalSamples);

    for (int i = 0;
         i < totalSamples;
         ++i)
    {
        double value =
            audio[i] *
            MASTER_GAIN *
            volumeMultiplier;

        value =
            std::tanh(value);

        samples[i] =
            static_cast<short>(
                value * 32767.0
            );
    }

    return true;
}

// ============================================================
// WAV WRITER
// ============================================================

bool SaveWavFile(
    const std::string& filename,
    const std::vector<short>& samples)
{
    std::ofstream file(
        filename,
        std::ios::binary
    );

    if (!file)
        return false;

    const uint32_t dataSize =
        static_cast<uint32_t>(
            samples.size() *
            sizeof(short)
        );

    const uint32_t fmtSize = 16;
    const uint16_t audioFormat = 1;
    const uint16_t channels = 1;
    const uint32_t sampleRate = SAMPLE_RATE;
    const uint16_t bitsPerSample = 16;

    const uint16_t blockAlign =
        channels *
        bitsPerSample /
        8;

    const uint32_t byteRate =
        sampleRate *
        blockAlign;

    const uint32_t riffSize =
        4 +
        8 + fmtSize +
        8 + dataSize;

    file.write(
        "RIFF",
        4
    );

    file.write(
        reinterpret_cast<const char*>(
            &riffSize
        ),
        sizeof(riffSize)
    );

    file.write(
        "WAVE",
        4
    );

    file.write(
        "fmt ",
        4
    );

    file.write(
        reinterpret_cast<const char*>(
            &fmtSize
        ),
        sizeof(fmtSize)
    );

    file.write(
        reinterpret_cast<const char*>(
            &audioFormat
        ),
        sizeof(audioFormat)
    );

    file.write(
        reinterpret_cast<const char*>(
            &channels
        ),
        sizeof(channels)
    );

    file.write(
        reinterpret_cast<const char*>(
            &sampleRate
        ),
        sizeof(sampleRate)
    );

    file.write(
        reinterpret_cast<const char*>(
            &byteRate
        ),
        sizeof(byteRate)
    );

    file.write(
        reinterpret_cast<const char*>(
            &blockAlign
        ),
        sizeof(blockAlign)
    );

    file.write(
        reinterpret_cast<const char*>(
            &bitsPerSample
        ),
        sizeof(bitsPerSample)
    );

    file.write(
        "data",
        4
    );

    file.write(
        reinterpret_cast<const char*>(
            &dataSize
        ),
        sizeof(dataSize)
    );

    if (dataSize > 0)
    {
        file.write(
            reinterpret_cast<const char*>(
                samples.data()
            ),
            dataSize
        );
    }

    return file.good();
}

// ============================================================
// TEMPORARY WAV FILE
// ============================================================

std::string MakeTemporaryWav()
{
    char tempPath[MAX_PATH] = {};

    DWORD pathLength =
        GetTempPathA(
            MAX_PATH,
            tempPath
        );

    if (pathLength == 0 ||
        pathLength >= MAX_PATH)
    {
        return "";
    }

    char tempFile[MAX_PATH] = {};

    if (GetTempFileNameA(
        tempPath,
        "MTE",
        0,
        tempFile) == 0)
    {
        return "";
    }

    // GetTempFileNameA creates a .tmp file.
    // Delete that file and use the same unique name
    // with a .wav extension instead.

    DeleteFileA(tempFile);

    std::string filename = tempFile;

    size_t dot =
        filename.find_last_of('.');

    if (dot != std::string::npos)
    {
        filename =
            filename.substr(0, dot);
    }

    filename += ".wav";

    return filename;
}
// ============================================================
// SHELL QUOTE
// ============================================================

std::string QuoteArgument(
    const std::string& value)
{
    std::string result = "\"";

    for (char c : value)
    {
        if (c == '"')
            result += "\\\"";
        else
            result += c;
    }

    result += "\"";

    return result;
}

// ============================================================
// RUN FFMPEG
// ============================================================
bool RunFFmpeg(
    const std::string& arguments)
{
    std::string command =
        QuoteArgument(FFMPEG_PATH) +
        " " +
        arguments;

    MessageBoxA(
        mainWindow,
        command.c_str(),
        "FFmpeg Command",
        MB_OK | MB_ICONINFORMATION
    );

    STARTUPINFOA startupInfo = {};
    PROCESS_INFORMATION processInfo = {};

    startupInfo.cb = sizeof(startupInfo);

    std::vector<char> commandLine(
        command.begin(),
        command.end()
    );

    commandLine.push_back('\0');

    BOOL created =
        CreateProcessA(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo
        );

    if (!created)
    {
        DWORD error = GetLastError();

        std::string message =
            "CreateProcess failed.\r\n\r\nError code: " +
            std::to_string(error);

        MessageBoxA(
            mainWindow,
            message.c_str(),
            "FFmpeg Error",
            MB_OK | MB_ICONERROR
        );

        return false;
    }

    WaitForSingleObject(
        processInfo.hProcess,
        INFINITE
    );

    DWORD exitCode = 1;

    GetExitCodeProcess(
        processInfo.hProcess,
        &exitCode
    );

    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    if (exitCode != 0)
    {
        std::string message =
            "FFmpeg exited with error code " +
            std::to_string(exitCode) +
            "\r\n\r\nCommand:\r\n" +
            command;

        MessageBoxA(
            mainWindow,
            message.c_str(),
            "FFmpeg Error",
            MB_OK | MB_ICONERROR
        );

        return false;
    }

    return true;
}
// ============================================================
// CHECK FFMPEG
// ============================================================

bool CheckFFmpeg()
{
    STARTUPINFOA startupInfo = {};
    PROCESS_INFORMATION processInfo = {};

    startupInfo.cb =
        sizeof(startupInfo);

    std::string command =
        QuoteArgument(
            FFMPEG_PATH
        ) +
        " -version";

    std::vector<char> commandLine(
        command.begin(),
        command.end()
    );

    commandLine.push_back('\0');

    BOOL created =
        CreateProcessA(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo
        );

    if (!created)
        return false;

    WaitForSingleObject(
        processInfo.hProcess,
        5000
    );

    DWORD exitCode = 1;

    GetExitCodeProcess(
        processInfo.hProcess,
        &exitCode
    );

    CloseHandle(
        processInfo.hProcess
    );

    CloseHandle(
        processInfo.hThread
    );

    return exitCode == 0;
}

// ============================================================
// EXPORT WAV
// ============================================================

void ExportCurrentTrackWav()
{
    std::string text =
        GetEditorText();

    if (text.empty())
    {
        MessageBoxA(
            mainWindow,
            "The editor is empty.",
            "EXPORT WAV",
            MB_OK | MB_ICONINFORMATION
        );

        return;
    }

    std::vector<short> samples;

    if (!GenerateAudio(
        text,
        currentTempo.load(),
        currentPitch.load(),
        GetVolumeMultiplier(
            volumeBoost.load()
        ),
        samples))
    {
        MessageBoxA(
            mainWindow,
            "Could not generate audio.",
            "EXPORT WAV",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    char filename[MAX_PATH] =
        "track.wav";

    OPENFILENAMEA dialog = {};

    dialog.lStructSize =
        sizeof(OPENFILENAMEA);

    dialog.hwndOwner =
        mainWindow;

    dialog.lpstrFilter =
        "WAV Audio (*.wav)\0*.wav\0"
        "All Files (*.*)\0*.*\0";

    dialog.lpstrFile =
        filename;

    dialog.nMaxFile =
        MAX_PATH;

    dialog.Flags =
        OFN_OVERWRITEPROMPT |
        OFN_PATHMUSTEXIST;

    dialog.lpstrDefExt =
        "wav";

    if (!GetSaveFileNameA(&dialog))
        return;

    if (!SaveWavFile(
        filename,
        samples))
    {
        MessageBoxA(
            mainWindow,
            "Could not save the WAV file.",
            "EXPORT WAV",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    MessageBoxA(
        mainWindow,
        "WAV exported successfully.",
        "EXPORT WAV",
        MB_OK | MB_ICONINFORMATION
    );
}

// ============================================================
// EXPORT MP3
// ============================================================

void ExportCurrentTrackMp3()
{
    std::string text =
        GetEditorText();

    if (text.empty())
    {
        MessageBoxA(
            mainWindow,
            "The editor is empty.",
            "EXPORT MP3",
            MB_OK | MB_ICONINFORMATION
        );

        return;
    }

    if (!CheckFFmpeg())
    {
        MessageBoxA(
            mainWindow,
            "FFmpeg could not be found.\r\n\r\n"
            "Put ffmpeg.exe beside this program "
            "or update FFMPEG_PATH in the source code.",
            "EXPORT MP3",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    std::vector<short> samples;

    if (!GenerateAudio(
        text,
        currentTempo.load(),
        currentPitch.load(),
        GetVolumeMultiplier(
            volumeBoost.load()
        ),
        samples))
    {
        MessageBoxA(
            mainWindow,
            "Could not generate audio.",
            "EXPORT MP3",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    std::string temporaryWav =
        MakeTemporaryWav();

    if (temporaryWav.empty() ||
        !SaveWavFile(
            temporaryWav,
            samples))
    {
        MessageBoxA(
            mainWindow,
            "Could not create temporary WAV audio.",
            "EXPORT MP3",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    char filename[MAX_PATH] =
        "track.mp3";

    OPENFILENAMEA dialog = {};

    dialog.lStructSize =
        sizeof(OPENFILENAMEA);

    dialog.hwndOwner =
        mainWindow;

    dialog.lpstrFilter =
        "MP3 Audio (*.mp3)\0*.mp3\0"
        "All Files (*.*)\0*.*\0";

    dialog.lpstrFile =
        filename;

    dialog.nMaxFile =
        MAX_PATH;

    dialog.Flags =
        OFN_OVERWRITEPROMPT |
        OFN_PATHMUSTEXIST;

    dialog.lpstrDefExt =
        "mp3";

    if (!GetSaveFileNameA(&dialog))
    {
        DeleteFileA(
            temporaryWav.c_str()
        );

        return;
    }

    std::string args =
    "-y -i " +
    QuoteArgument(
        temporaryWav
    ) +
    " -codec:a libmp3lame "
    "-b:a 320k " +
    QuoteArgument(
        filename
    );

bool success =
    RunFFmpeg(args);

    DeleteFileA(
        temporaryWav.c_str()
    );

    if (!success)
    {
        MessageBoxA(
            mainWindow,
            "FFmpeg could not create the MP3.",
            "EXPORT MP3",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    MessageBoxA(
        mainWindow,
        "MP3 exported successfully.",
        "EXPORT MP3",
        MB_OK | MB_ICONINFORMATION
    );
}

// ============================================================
// END OF PART 1
// ============================================================
// ============================================================
// EXPORT YOUTUBE MP4
// ============================================================

void ExportCurrentTrackMp4()
{
    std::string text =
        GetEditorText();

    if (text.empty())
    {
        MessageBoxA(
            mainWindow,
            "The editor is empty.",
            "EXPORT YOUTUBE MP4",
            MB_OK | MB_ICONINFORMATION
        );

        return;
    }

    if (!CheckFFmpeg())
    {
        MessageBoxA(
            mainWindow,
            "FFmpeg could not be found.\r\n\r\n"
            "Put ffmpeg.exe beside this program "
            "or update FFMPEG_PATH in the source code.",
            "EXPORT YOUTUBE MP4",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    // --------------------------------------------------------
    // Select artwork.
    // --------------------------------------------------------

    char imageFilename[MAX_PATH] = "";

    OPENFILENAMEA imageDialog = {};

    imageDialog.lStructSize =
        sizeof(OPENFILENAMEA);

    imageDialog.hwndOwner =
        mainWindow;

    imageDialog.lpstrFilter =
        "Image Files (*.jpg;*.jpeg;*.png)\0"
        "*.jpg;*.jpeg;*.png\0"
        "JPEG Files (*.jpg;*.jpeg)\0"
        "*.jpg;*.jpeg\0"
        "PNG Files (*.png)\0"
        "*.png\0"
        "All Files (*.*)\0"
        "*.*\0";

    imageDialog.lpstrFile =
        imageFilename;

    imageDialog.nMaxFile =
        MAX_PATH;

    imageDialog.Flags =
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameA(
        &imageDialog))
        return;

    // --------------------------------------------------------
    // Generate audio.
    // --------------------------------------------------------

    std::vector<short> samples;

    if (!GenerateAudio(
        text,
        currentTempo.load(),
        currentPitch.load(),
        GetVolumeMultiplier(
            volumeBoost.load()
        ),
        samples))
    {
        MessageBoxA(
            mainWindow,
            "Could not generate audio.",
            "EXPORT YOUTUBE MP4",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    if (samples.empty())
    {
        MessageBoxA(
            mainWindow,
            "The generated audio is empty.",
            "EXPORT YOUTUBE MP4",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    // --------------------------------------------------------
    // Write temporary WAV.
    // --------------------------------------------------------

    std::string temporaryWav =
        MakeTemporaryWav();

    if (temporaryWav.empty() ||
        !SaveWavFile(
            temporaryWav,
            samples))
    {
        MessageBoxA(
            mainWindow,
            "Could not create temporary WAV audio.",
            "EXPORT YOUTUBE MP4",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    // --------------------------------------------------------
    // Choose MP4 destination.
    // --------------------------------------------------------

    char outputFilename[MAX_PATH] =
        "youtube_track.mp4";

    OPENFILENAMEA outputDialog = {};

    outputDialog.lStructSize =
        sizeof(OPENFILENAMEA);

    outputDialog.hwndOwner =
        mainWindow;

    outputDialog.lpstrFilter =
        "MP4 Video (*.mp4)\0*.mp4\0"
        "All Files (*.*)\0*.*\0";

    outputDialog.lpstrFile =
        outputFilename;

    outputDialog.nMaxFile =
        MAX_PATH;

    outputDialog.Flags =
        OFN_OVERWRITEPROMPT |
        OFN_PATHMUSTEXIST;

    outputDialog.lpstrDefExt =
        "mp4";

    if (!GetSaveFileNameA(
        &outputDialog))
    {
        DeleteFileA(
            temporaryWav.c_str()
        );

        return;
    }

    // --------------------------------------------------------
    // Create YouTube MP4.
    //
    // 1920x1080 video
    // H.264 video
    // AAC audio
    // 30 FPS
    // Artwork displayed for entire song
    // --------------------------------------------------------

    std::string args =
        "-y "
        "-loop 1 "
        "-framerate 30 "
        "-i " +
        QuoteArgument(
            imageFilename
        ) +
        " -i " +
        QuoteArgument(
            temporaryWav
        ) +
        " -map 0:v:0 "
        "-map 1:a:0 "
        "-vf \"scale=1920:1080:force_original_aspect_ratio=decrease,"
        "pad=1920:1080:(ow-iw)/2:(oh-ih)/2,"
        "format=yuv420p\" "
        "-c:v libx264 "
        "-preset medium "
        "-crf 18 "
        "-r 30 "
        "-c:a aac "
        "-b:a 320k "
        "-shortest " +
        QuoteArgument(
            outputFilename
        );

    bool success =
        RunFFmpeg(args);

    DeleteFileA(
        temporaryWav.c_str()
    );

    if (!success)
    {
        MessageBoxA(
            mainWindow,
            "FFmpeg could not create the MP4.",
            "EXPORT YOUTUBE MP4",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    MessageBoxA(
        mainWindow,
        "YouTube MP4 exported successfully.",
        "EXPORT YOUTUBE MP4",
        MB_OK | MB_ICONINFORMATION
    );
}

// ============================================================
// PLAY SONG
// ============================================================

void PlaySongText(
    const std::string& songText)
{
    if (playing || songText.empty())
        return;

    playing = true;
    playingAll = false;
    stopRequested = false;

    std::vector<NoteEvent> notes;
    std::vector<DrumEvent> drums;

    double fileTempo = 120.0;
    double loopLengthBeats = 4.0;

    LoadSongText(
        songText,
        notes,
        drums,
        fileTempo,
        loopLengthBeats
    );

    if (
        notes.empty() &&
        drums.empty())
    {
        playing = false;
        return;
    }

    WAVEFORMATEX format = {};

    format.wFormatTag =
        WAVE_FORMAT_PCM;

    format.nChannels = 1;

    format.nSamplesPerSec =
        SAMPLE_RATE;

    format.wBitsPerSample = 16;

    format.nBlockAlign = 2;

    format.nAvgBytesPerSec =
        SAMPLE_RATE * 2;

    HWAVEOUT audioDevice = nullptr;

    if (
        waveOutOpen(
            &audioDevice,
            WAVE_MAPPER,
            &format,
            0,
            0,
            CALLBACK_NULL
        ) != MMSYSERR_NOERROR)
    {
        playing = false;
        return;
    }

    std::vector<short> samples;

    if (!GenerateAudio(
        songText,
        currentTempo.load(),
        currentPitch.load(),
        GetVolumeMultiplier(
            volumeBoost.load()
        ),
        samples))
    {
        waveOutClose(
            audioDevice
        );

        playing = false;
        return;
    }

    WAVEHDR header = {};

    header.lpData =
        reinterpret_cast<LPSTR>(
            samples.data()
        );

    header.dwBufferLength =
        static_cast<DWORD>(
            samples.size() *
            sizeof(short)
        );

    if (
        waveOutPrepareHeader(
            audioDevice,
            &header,
            sizeof(WAVEHDR)
        ) != MMSYSERR_NOERROR)
    {
        waveOutClose(
            audioDevice
        );

        playing = false;
        return;
    }

    if (
        waveOutWrite(
            audioDevice,
            &header,
            sizeof(WAVEHDR)
        ) != MMSYSERR_NOERROR)
    {
        waveOutUnprepareHeader(
            audioDevice,
            &header,
            sizeof(WAVEHDR)
        );

        waveOutClose(
            audioDevice
        );

        playing = false;
        return;
    }

    while (!stopRequested)
    {
        if (header.dwFlags & WHDR_DONE)
            break;

        Sleep(1);
    }

    waveOutReset(
        audioDevice
    );

    waveOutUnprepareHeader(
        audioDevice,
        &header,
        sizeof(WAVEHDR)
    );

    waveOutClose(
        audioDevice
    );

    playing = false;
}

// ============================================================
// PLAY ALL SAVED TRACKS
// ============================================================

void PlayAllSavedTracks()
{
    if (
        playing ||
        savedTracks.empty())
        return;

    playing = true;
    playingAll = true;
    stopRequested = false;

    std::vector<short> combinedSamples;

    double tempo =
        currentTempo.load();

    int pitch =
        currentPitch.load();

    double multiplier =
        GetVolumeMultiplier(
            volumeBoost.load()
        );

    for (
        const std::string& track :
        savedTracks)
    {
        if (stopRequested)
            break;

        std::vector<short> trackSamples;

        if (GenerateAudio(
            track,
            tempo,
            pitch,
            multiplier,
            trackSamples))
        {
            combinedSamples.insert(
                combinedSamples.end(),
                trackSamples.begin(),
                trackSamples.end()
            );
        }
    }

    if (
        combinedSamples.empty() ||
        stopRequested)
    {
        playing = false;
        playingAll = false;
        return;
    }

    WAVEFORMATEX format = {};

    format.wFormatTag =
        WAVE_FORMAT_PCM;

    format.nChannels = 1;

    format.nSamplesPerSec =
        SAMPLE_RATE;

    format.wBitsPerSample = 16;

    format.nBlockAlign = 2;

    format.nAvgBytesPerSec =
        SAMPLE_RATE * 2;

    HWAVEOUT audioDevice = nullptr;

    if (
        waveOutOpen(
            &audioDevice,
            WAVE_MAPPER,
            &format,
            0,
            0,
            CALLBACK_NULL
        ) != MMSYSERR_NOERROR)
    {
        playing = false;
        playingAll = false;
        return;
    }

    WAVEHDR header = {};

    header.lpData =
        reinterpret_cast<LPSTR>(
            combinedSamples.data()
        );

    header.dwBufferLength =
        static_cast<DWORD>(
            combinedSamples.size() *
            sizeof(short)
        );

    if (
        waveOutPrepareHeader(
            audioDevice,
            &header,
            sizeof(WAVEHDR)
        ) != MMSYSERR_NOERROR)
    {
        waveOutClose(
            audioDevice
        );

        playing = false;
        playingAll = false;

        return;
    }

    while (!stopRequested)
    {
        header.dwFlags &= ~WHDR_DONE;

        if (
            waveOutWrite(
                audioDevice,
                &header,
                sizeof(WAVEHDR)
            ) != MMSYSERR_NOERROR)
            break;

        while (!stopRequested)
        {
            if (header.dwFlags & WHDR_DONE)
                break;

            Sleep(1);
        }

        if (stopRequested)
            break;

        if (!looping.load())
            break;
    }

    waveOutReset(
        audioDevice
    );

    waveOutUnprepareHeader(
        audioDevice,
        &header,
        sizeof(WAVEHDR)
    );

    waveOutClose(
        audioDevice
    );

    playing = false;
    playingAll = false;
}

// ============================================================
// PLAY EDITOR
// ============================================================

void PlayEditor()
{
    std::string text =
        GetEditorText();

    if (text.empty())
        return;

    if (playing)
    {
        stopRequested = true;
        return;
    }

    std::thread(
        PlaySongText,
        text
    ).detach();
}

// ============================================================
// PLAY ALL
// ============================================================

void StartPlayAll()
{
    if (savedTracks.empty())
    {
        MessageBoxA(
            mainWindow,
            "There are no saved tracks.",
            "PLAY ALL",
            MB_OK | MB_ICONINFORMATION
        );

        return;
    }

    if (playing)
    {
        stopRequested = true;
        return;
    }

    std::thread(
        PlayAllSavedTracks
    ).detach();
}

// ============================================================
// DISPLAY UPDATES
// ============================================================

void UpdateTempoDisplay()
{
    if (!tempoLabel)
        return;

    char text[64];

    std::snprintf(
        text,
        sizeof(text),
        "TEMPO: %d",
        static_cast<int>(
            std::round(
                currentTempo.load()
            )
        )
    );

    SetWindowTextA(
        tempoLabel,
        text
    );
}

void UpdatePitchDisplay()
{
    if (!pitchLabel)
        return;

    char text[64];

    int pitch =
        currentPitch.load();

    if (pitch > 0)
    {
        std::snprintf(
            text,
            sizeof(text),
            "PITCH: +%d",
            pitch
        );
    }
    else
    {
        std::snprintf(
            text,
            sizeof(text),
            "PITCH: %d",
            pitch
        );
    }

    SetWindowTextA(
        pitchLabel,
        text
    );
}

void UpdateBoostDisplay()
{
    if (!boostButton)
        return;

    char text[64];

    std::snprintf(
        text,
        sizeof(text),
        "BOOST: %.1fx",
        GetVolumeMultiplier(
            volumeBoost.load()
        )
    );

    SetWindowTextA(
        boostButton,
        text
    );
}

// ============================================================
// SAVE CURRENT TRACK
// ============================================================

void SaveCurrentTrack()
{
    std::string text =
        GetEditorText();

    if (text.empty())
    {
        MessageBoxA(
            mainWindow,
            "The editor is empty.",
            "SAVE TRACK",
            MB_OK | MB_ICONINFORMATION
        );

        return;
    }

    savedTracks.push_back(text);

    MessageBoxA(
        mainWindow,
        "Track saved.",
        "SAVE TRACK",
        MB_OK | MB_ICONINFORMATION
    );
}

// ============================================================
// LOAD TRACK
// ============================================================

void LoadTrack(size_t index)
{
    if (index >= savedTracks.size())
        return;

    SetEditorText(
        savedTracks[index]
    );
}

// ============================================================
// DELETE TRACK
// ============================================================

void DeleteTrack(size_t index)
{
    if (index >= savedTracks.size())
        return;

    savedTracks.erase(
        savedTracks.begin() +
        static_cast<std::ptrdiff_t>(
            index
        )
    );
}

// ============================================================
// COMBINE ALL SAVED TRACKS
// ============================================================

std::string CombineSavedTracks()
{
    if (savedTracks.empty())
        return "";

    double totalLength = 0.0;

    for (
        const std::string& track :
        savedTracks)
    {
        std::vector<NoteEvent> notes;
        std::vector<DrumEvent> drums;

        double tempo = 120.0;
        double length = 4.0;

        LoadSongText(
            track,
            notes,
            drums,
            tempo,
            length
        );

        if (length > 0.0)
            totalLength += length;
    }

    std::ostringstream output;

    output
        << "TRACK COMBINED\r\n"
        << "TEMPO "
        << static_cast<int>(
            std::round(
                currentTempo.load()
            )
        )
        << "\r\n"
        << "LENGTH "
        << totalLength
        << "\r\n";

    double offset = 0.0;

    for (
        const std::string& track :
        savedTracks)
    {
        std::vector<NoteEvent> notes;
        std::vector<DrumEvent> drums;

        double tempo = 120.0;
        double trackLength = 4.0;

        LoadSongText(
            track,
            notes,
            drums,
            tempo,
            trackLength
        );

        std::istringstream input(track);
        std::string line;

        while (std::getline(
            input,
            line))
        {
            std::istringstream lineStream(
                line
            );

            std::string command;

            lineStream >>
                command;

            if (command.empty())
                continue;

            std::string upperCommand =
                upper(command);

            if (
                upperCommand == "TRACK" ||
                upperCommand == "TEMPO" ||
                upperCommand == "LENGTH")
            {
                continue;
            }

            if (
                upperCommand == "NOTE" ||
                upperCommand == "PIANO" ||
                upperCommand == "BASS" ||
                upperCommand == "GUITAR" ||
                upperCommand == "SYNTH" ||
                upperCommand == "ORGAN" ||
                upperCommand == "FLUTE" ||
                upperCommand == "TRUMPET" ||
                upperCommand == "BELL")
            {
                std::string noteName;
                double startBeat = 0.0;
                double durationBeats = 1.0;

                lineStream
                    >> noteName
                    >> startBeat
                    >> durationBeats;

                output
                    << command
                    << " "
                    << noteName
                    << " "
                    << (startBeat + offset)
                    << " "
                    << durationBeats
                    << "\r\n";

                continue;
            }

            if (upperCommand == "DRUM")
            {
                std::string type;
                double startBeat = 0.0;

                lineStream
                    >> type
                    >> startBeat;

                output
                    << command
                    << " "
                    << type
                    << " "
                    << (startBeat + offset)
                    << "\r\n";
            }
        }

        offset += trackLength;
    }

    return output.str();
}

// ============================================================
// SAVE COMBINED TRACK
// ============================================================

void SaveCombinedTrackToFile()
{
    if (savedTracks.empty())
    {
        MessageBoxA(
            mainWindow,
            "There are no saved tracks to combine.",
            "COMBINE + SAVE",
            MB_OK | MB_ICONINFORMATION
        );

        return;
    }

    std::string combined =
        CombineSavedTracks();

    if (combined.empty())
    {
        MessageBoxA(
            mainWindow,
            "Could not combine the tracks.",
            "COMBINE + SAVE",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    char filename[MAX_PATH] =
        "combined_song.song";

    OPENFILENAMEA dialog = {};

    dialog.lStructSize =
        sizeof(OPENFILENAMEA);

    dialog.hwndOwner =
        mainWindow;

    dialog.lpstrFilter =
        "Music Track Files (*.song)\0*.song\0"
        "Text Files (*.txt)\0*.txt\0"
        "All Files (*.*)\0*.*\0";

    dialog.lpstrFile =
        filename;

    dialog.nMaxFile =
        MAX_PATH;

    dialog.Flags =
        OFN_OVERWRITEPROMPT |
        OFN_PATHMUSTEXIST;

    dialog.lpstrDefExt =
        "song";

    if (!GetSaveFileNameA(
        &dialog))
        return;

    std::ofstream file(
        filename,
        std::ios::out |
        std::ios::trunc
    );

    if (!file)
    {
        MessageBoxA(
            mainWindow,
            "Could not save the file.",
            "COMBINE + SAVE",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    file << combined;

    file.close();

    MessageBoxA(
        mainWindow,
        "All saved tracks were combined and saved.",
        "COMBINE + SAVE",
        MB_OK | MB_ICONINFORMATION
    );
}

// ============================================================
// IMPORT TRACKS
// ============================================================

void RebuildTrackControls();

void ImportTracksFromFile()
{
    char filename[MAX_PATH] = "";

    OPENFILENAMEA dialog = {};

    dialog.lStructSize =
        sizeof(OPENFILENAMEA);

    dialog.hwndOwner =
        mainWindow;

    dialog.lpstrFilter =
        "Music Track Files (*.song)\0*.song\0"
        "Text Files (*.txt)\0*.txt\0"
        "All Files (*.*)\0*.*\0";

    dialog.lpstrFile =
        filename;

    dialog.nMaxFile =
        MAX_PATH;

    dialog.Flags =
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameA(
        &dialog))
        return;

    std::ifstream file(
        filename,
        std::ios::in
    );

    if (!file)
    {
        MessageBoxA(
            mainWindow,
            "Could not open the file.",
            "IMPORT",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    std::stringstream buffer;

    buffer << file.rdbuf();

    file.close();

    std::string contents =
        buffer.str();

    if (contents.empty())
    {
        MessageBoxA(
            mainWindow,
            "The selected file is empty.",
            "IMPORT",
            MB_OK | MB_ICONWARNING
        );

        return;
    }

    savedTracks.push_back(
        contents
    );

    RebuildTrackControls();

    MessageBoxA(
        mainWindow,
        "Track imported.",
        "IMPORT",
        MB_OK | MB_ICONINFORMATION
    );
}

// ============================================================
// TEMPO / PITCH / BOOST
// ============================================================

void ChangeTempo(double amount)
{
    double tempo =
        currentTempo.load();

    tempo += amount;

    tempo =
        std::max(
            MIN_TEMPO,
            std::min(
                MAX_TEMPO,
                tempo
            )
        );

    currentTempo =
        tempo;

    UpdateTempoDisplay();
}

void ChangePitch(int amount)
{
    int pitch =
        currentPitch.load();

    pitch += amount;

    pitch =
        std::max(
            MIN_PITCH,
            std::min(
                MAX_PITCH,
                pitch
            )
        );

    currentPitch =
        pitch;

    UpdatePitchDisplay();
}

void ChangeBoost()
{
    int boost =
        volumeBoost.load();

    boost++;

    if (boost > 3)
        boost = 0;

    volumeBoost =
        boost;

    UpdateBoostDisplay();
}

// ============================================================
// CHORDS
// ============================================================

std::vector<std::string> GetChordRoots()
{
    return
    {
        "C","C#","D","D#","E","F",
        "F#","G","G#","A","A#","B"
    };
}

std::vector<std::string> GetChordTypes()
{
    return
    {
        "MAJOR",
        "MINOR",
        "MINOR 7",
        "MAJOR 7"
    };
}

std::string NoteFromSemitone(
    int semitone)
{
    static const char* names[] =
    {
        "C","C#","D","D#","E","F",
        "F#","G","G#","A","A#","B"
    };

    int octave =
        4 +
        semitone / 12;

    int index =
        semitone % 12;

    if (index < 0)
    {
        index += 12;
        octave--;
    }

    return
        std::string(names[index]) +
        std::to_string(octave);
}

std::vector<std::string> GetChordNotes(
    const std::string& root,
    const std::string& type)
{
    std::vector<std::string> roots =
        GetChordRoots();

    int rootIndex = 0;

    for (
        int i = 0;
        i < static_cast<int>(
            roots.size());
        ++i)
    {
        if (roots[i] == root)
        {
            rootIndex = i;
            break;
        }
    }

    std::vector<int> intervals;

    if (type == "MAJOR")
        intervals = {0, 4, 7};
    else if (type == "MINOR")
        intervals = {0, 3, 7};
    else if (type == "MINOR 7")
        intervals = {0, 3, 7, 10};
    else if (type == "MAJOR 7")
        intervals = {0, 4, 7, 11};

    std::vector<std::string> result;

    for (int interval : intervals)
    {
        result.push_back(
            NoteFromSemitone(
                rootIndex +
                interval
            )
        );
    }

    return result;
}

// ============================================================
// DRUM LIST
// ============================================================

std::vector<std::string> GetDrums()
{
    return
    {
        "KICK",
        "SNARE",
        "HIHAT",
        "OPEN HIHAT",
        "LOW TOM",
        "MID TOM",
        "HIGH TOM",
        "CRASH",
        "RIDE",
        "RIMSHOT"
    };
}

std::string DrumCommandName(
    const std::string& displayName)
{
    if (displayName == "OPEN HIHAT")
        return "OPEN_HIHAT";

    if (displayName == "LOW TOM")
        return "LOW_TOM";

    if (displayName == "MID TOM")
        return "MID_TOM";

    if (displayName == "HIGH TOM")
        return "HIGH_TOM";

    return displayName;
}

// ============================================================
// BUILD INSTRUMENT MENU
// ============================================================

HMENU BuildInstrumentMenu()
{
    HMENU menu =
        CreatePopupMenu();

    for (
        const std::string& instrument :
        GetInstruments())
    {
        HMENU noteMenu =
            CreatePopupMenu();

        for (
            const std::string& note :
            GetNotes())
        {
            UINT id =
                ID_NOTE_BASE +
                static_cast<UINT>(
                    noteCommands.size()
                );

            noteCommands[id] =
                instrument +
                " " +
                note +
                " 0 1";

            AppendMenuA(
                noteMenu,
                MF_STRING,
                id,
                note.c_str()
            );
        }

        AppendMenuA(
            menu,
            MF_POPUP,
            reinterpret_cast<UINT_PTR>(
                noteMenu
            ),
            instrument.c_str()
        );
    }

    return menu;
}

// ============================================================
// BUILD CHORD MENU
// ============================================================

HMENU BuildChordMenu()
{
    HMENU menu =
        CreatePopupMenu();

    for (
        const std::string& instrument :
        GetInstruments())
    {
        HMENU instrumentMenu =
            CreatePopupMenu();

        for (
            const std::string& root :
            GetChordRoots())
        {
            HMENU rootMenu =
                CreatePopupMenu();

            for (
                const std::string& type :
                GetChordTypes())
            {
                UINT id =
                    ID_CHORD_BASE +
                    static_cast<UINT>(
                        chordCommands.size()
                    );

                std::string command;

                for (
                    const std::string& note :
                    GetChordNotes(
                        root,
                        type))
                {
                    command +=
                        instrument +
                        " " +
                        note +
                        " 0 1\r\n";
                }

                chordCommands[id] =
                    command;

                std::string label =
                    root +
                    " " +
                    type;

                AppendMenuA(
                    rootMenu,
                    MF_STRING,
                    id,
                    label.c_str()
                );
            }

            AppendMenuA(
                instrumentMenu,
                MF_POPUP,
                reinterpret_cast<UINT_PTR>(
                    rootMenu
                ),
                root.c_str()
            );
        }

        AppendMenuA(
            menu,
            MF_POPUP,
            reinterpret_cast<UINT_PTR>(
                instrumentMenu
            ),
            instrument.c_str()
        );
    }

    return menu;
}

// ============================================================
// BUILD DRUM MENU
// ============================================================

HMENU BuildDrumMenu()
{
    HMENU menu =
        CreatePopupMenu();

    for (
        const std::string& drum :
        GetDrums())
    {
        HMENU beatMenu =
            CreatePopupMenu();

        std::string drumType =
            DrumCommandName(
                drum
            );

        for (int i = 0; i < 16; ++i)
        {
            double beat =
                static_cast<double>(i) *
                0.5;

            UINT id =
                ID_DRUM_BASE +
                static_cast<UINT>(
                    drumCommands.size()
                );

            char beatText[32];

            if (
                std::fabs(
                    beat -
                    std::round(beat)
                ) < 0.001)
            {
                std::snprintf(
                    beatText,
                    sizeof(beatText),
                    "BEAT %d",
                    static_cast<int>(
                        beat
                    )
                );
            }
            else
            {
                std::snprintf(
                    beatText,
                    sizeof(beatText),
                    "BEAT %.1f",
                    beat
                );
            }

            char commandText[128];

            std::snprintf(
                commandText,
                sizeof(commandText),
                "DRUM %s %.1f",
                drumType.c_str(),
                beat
            );

            drumCommands[id] =
                commandText;

            AppendMenuA(
                beatMenu,
                MF_STRING,
                id,
                beatText
            );
        }

        AppendMenuA(
            menu,
            MF_POPUP,
            reinterpret_cast<UINT_PTR>(
                beatMenu
            ),
            drum.c_str()
        );
    }

    return menu;
}

// ============================================================
// SHOW MENUS
// ============================================================

void ShowInstrumentSelector()
{
    POINT point;

    GetCursorPos(&point);

    HMENU menu =
        BuildInstrumentMenu();

    TrackPopupMenu(
        menu,
        TPM_LEFTALIGN |
        TPM_TOPALIGN |
        TPM_RIGHTBUTTON,
        point.x,
        point.y,
        0,
        mainWindow,
        nullptr
    );

    DestroyMenu(menu);
}

void ShowChordSelector()
{
    POINT point;

    GetCursorPos(&point);

    HMENU menu =
        BuildChordMenu();

    TrackPopupMenu(
        menu,
        TPM_LEFTALIGN |
        TPM_TOPALIGN |
        TPM_RIGHTBUTTON,
        point.x,
        point.y,
        0,
        mainWindow,
        nullptr
    );

    DestroyMenu(menu);
}

void ShowDrumSelector()
{
    POINT point;

    GetCursorPos(&point);

    HMENU menu =
        BuildDrumMenu();

    TrackPopupMenu(
        menu,
        TPM_LEFTALIGN |
        TPM_TOPALIGN |
        TPM_RIGHTBUTTON,
        point.x,
        point.y,
        0,
        mainWindow,
        nullptr
    );

    DestroyMenu(menu);
}

// ============================================================
// DRAW BUTTON
// ============================================================

void DrawOwnerButton(
    LPDRAWITEMSTRUCT dis)
{
    if (!dis)
        return;

    HDC dc =
        dis->hDC;

    RECT rect =
        dis->rcItem;

    bool pressed =
        (dis->itemState &
         ODS_SELECTED) != 0;

    COLORREF background =
        pressed
            ? RGB(45, 45, 55)
            : COLUMN;

    HBRUSH backgroundBrush =
        CreateSolidBrush(
            background
        );

    FillRect(
        dc,
        &rect,
        backgroundBrush
    );

    DeleteObject(
        backgroundBrush
    );

    COLORREF borderColor =
        (dis->itemState &
         ODS_FOCUS)
            ? RGB(0, 255, 0)
            : COLUMN_BORDER;

    HPEN pen =
        CreatePen(
            PS_SOLID,
            1,
            borderColor
        );

    HPEN oldPen =
        static_cast<HPEN>(
            SelectObject(
                dc,
                pen
            )
        );

    HBRUSH oldBrush =
        static_cast<HBRUSH>(
            SelectObject(
                dc,
                GetStockObject(
                    NULL_BRUSH
                )
            )
        );

    Rectangle(
        dc,
        rect.left,
        rect.top,
        rect.right,
        rect.bottom
    );

    SelectObject(
        dc,
        oldBrush
    );

    SelectObject(
        dc,
        oldPen
    );

    DeleteObject(pen);

    char text[256];

    GetWindowTextA(
        dis->hwndItem,
        text,
        sizeof(text)
    );

    SetBkMode(
        dc,
        TRANSPARENT
    );

    SetTextColor(
        dc,
        BUTTON_TEXT
    );

    HFONT font =
        static_cast<HFONT>(
            GetStockObject(
                DEFAULT_GUI_FONT
            )
        );

    HFONT oldFont =
        static_cast<HFONT>(
            SelectObject(
                dc,
                font
            )
        );

    if (pressed)
        OffsetRect(
            &rect,
            1,
            1
        );

    DrawTextA(
        dc,
        text,
        -1,
        &rect,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    SelectObject(
        dc,
        oldFont
    );
}

// ============================================================
// CREATE CUSTOM BUTTON
// ============================================================

HWND CreateCustomButton(
    const char* text,
    int x,
    int y,
    int width,
    int height,
    UINT id)
{
    return CreateWindowExA(
        0,
        "BUTTON",
        text,
        WS_CHILD |
        WS_VISIBLE |
        BS_OWNERDRAW,
        x,
        y,
        width,
        height,
        mainWindow,
        reinterpret_cast<HMENU>(
            static_cast<UINT_PTR>(id)
        ),
        GetModuleHandleA(nullptr),
        nullptr
    );
}

// ============================================================
// TRACK CONTROLS
// ============================================================

UINT MakeTrackCommand(
    UINT base,
    size_t index)
{
    return
        base +
        static_cast<UINT>(
            index
        );
}

void RebuildTrackControls()
{
    std::vector<HWND> children;

    HWND child =
        GetWindow(
            mainWindow,
            GW_CHILD
        );

    while (child)
    {
        HWND next =
            GetWindow(
                child,
                GW_HWNDNEXT
            );

        LONG_PTR id =
            GetWindowLongPtrA(
                child,
                GWLP_ID
            );

        if (
            (id >= ID_LOAD_TRACK_BASE &&
             id <
             ID_LOAD_TRACK_BASE + 1000)
            ||
            (id >= ID_PLAY_TRACK_BASE &&
             id <
             ID_PLAY_TRACK_BASE + 1000)
            ||
            (id >= ID_DELETE_TRACK_BASE &&
             id <
             ID_DELETE_TRACK_BASE + 1000)
        )
        {
            children.push_back(
                child
            );
        }

        child = next;
    }

    for (HWND hwnd : children)
        DestroyWindow(hwnd);

    int y =
        TRACK_TOP -
        scrollY;

    for (
        size_t i = 0;
        i < savedTracks.size();
        ++i)
    {
        char label[64];

        std::snprintf(
            label,
            sizeof(label),
            "TRACK %d",
            static_cast<int>(
                i + 1
            )
        );

        HWND trackLabel =
            CreateWindowExA(
                0,
                "STATIC",
                label,
                WS_CHILD |
                WS_VISIBLE,
                LEFT_MARGIN,
                y,
                100,
                TRACK_BUTTON_HEIGHT,
                mainWindow,
                nullptr,
                GetModuleHandleA(
                    nullptr
                ),
                nullptr
            );

        HDC labelDC =
            GetDC(
                trackLabel
            );

        SetTextColor(
            labelDC,
            TEXT_COLOR
        );

        SetBkMode(
            labelDC,
            TRANSPARENT
        );

        ReleaseDC(
            trackLabel,
            labelDC
        );

        CreateCustomButton(
            "LOAD",
            120,
            y,
            90,
            TRACK_BUTTON_HEIGHT,
            MakeTrackCommand(
                ID_LOAD_TRACK_BASE,
                i
            )
        );

        CreateCustomButton(
            "PLAY",
            218,
            y,
            90,
            TRACK_BUTTON_HEIGHT,
            MakeTrackCommand(
                ID_PLAY_TRACK_BASE,
                i
            )
        );

        CreateCustomButton(
            "DELETE",
            316,
            y,
            90,
            TRACK_BUTTON_HEIGHT,
            MakeTrackCommand(
                ID_DELETE_TRACK_BASE,
                i
            )
        );

        y +=
            TRACK_BUTTON_HEIGHT +
            TRACK_GAP;
    }

    contentHeight =
        std::max(
            900,
            y + 100
        );

    InvalidateRect(
        mainWindow,
        nullptr,
        TRUE
    );
}

// ============================================================
// CREATE MAIN CONTROLS
// ============================================================

void CreateMainControls()
{
    playButton =
        CreateCustomButton(
            "PLAY",
            15,
            20,
            100,
            40,
            ID_PLAY
        );

    playAllButton =
        CreateCustomButton(
            "PLAY ALL",
            125,
            20,
            110,
            40,
            ID_PLAY_ALL
        );

    loopButton =
        CreateCustomButton(
            "LOOP: OFF",
            245,
            20,
            110,
            40,
            ID_LOOP
        );

    boostButton =
        CreateCustomButton(
            "BOOST: 1.0x",
            365,
            20,
            120,
            40,
            ID_BOOST
        );

    saveTrackButton =
        CreateCustomButton(
            "SAVE TRACK",
            500,
            20,
            130,
            40,
            ID_SAVE_TRACK
        );

    combineSaveButton =
        CreateCustomButton(
            "COMBINE + SAVE",
            640,
            20,
            150,
            40,
            ID_COMBINE_SAVE
        );

    importButton =
        CreateCustomButton(
            "IMPORT",
            800,
            20,
            100,
            40,
            ID_IMPORT_TRACKS
        );

    exportWavButton =
        CreateCustomButton(
            "EXPORT WAV",
            910,
            20,
            110,
            40,
            ID_EXPORT_WAV
        );

    exportMp3Button =
        CreateCustomButton(
            "EXPORT MP3",
            1030,
            20,
            110,
            40,
            ID_EXPORT_MP3
        );

    exportMp4Button =
        CreateCustomButton(
            "YOUTUBE MP4",
            910,
            65,
            130,
            40,
            ID_EXPORT_MP4
        );

    // --------------------------------------------------------
    // INSTRUMENTS
    // --------------------------------------------------------

    instrumentsButton =
        CreateCustomButton(
            "INSTRUMENTS",
            15,
            75,
            130,
            40,
            ID_INSTRUMENTS
        );

    chordsButton =
        CreateCustomButton(
            "CHORDS",
            155,
            75,
            110,
            40,
            ID_CHORDS
        );

    drumsButton =
        CreateCustomButton(
            "DRUMS",
            275,
            75,
            110,
            40,
            ID_DRUMS
        );

    // --------------------------------------------------------
    // TEMPO
    // --------------------------------------------------------

    tempoMinusButton =
        CreateCustomButton(
            "-",
            405,
            75,
            40,
            40,
            ID_TEMPO_MINUS
        );

    tempoPlusButton =
        CreateCustomButton(
            "+",
            495,
            75,
            40,
            40,
            ID_TEMPO_PLUS
        );

    tempoLabel =
        CreateWindowExA(
            0,
            "STATIC",
            "TEMPO: 120",
            WS_CHILD |
            WS_VISIBLE |
            SS_CENTER,
            445,
            75,
            50,
            40,
            mainWindow,
            nullptr,
            GetModuleHandleA(
                nullptr
            ),
            nullptr
        );

    // --------------------------------------------------------
    // PITCH
    // --------------------------------------------------------

    pitchMinusButton =
        CreateCustomButton(
            "-",
            405,
            125,
            40,
            40,
            ID_PITCH_MINUS
        );

    pitchPlusButton =
        CreateCustomButton(
            "+",
            495,
            125,
            40,
            40,
            ID_PITCH_PLUS
        );

    pitchLabel =
        CreateWindowExA(
            0,
            "STATIC",
            "PITCH: 0",
            WS_CHILD |
            WS_VISIBLE |
            SS_CENTER,
            445,
            125,
            50,
            40,
            mainWindow,
            nullptr,
            GetModuleHandleA(
                nullptr
            ),
            nullptr
        );

    // --------------------------------------------------------
    // EDITOR
    // --------------------------------------------------------

    songEditor =
        CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            "",
            WS_CHILD |
            WS_VISIBLE |
            WS_VSCROLL |
            WS_HSCROLL |
            ES_MULTILINE |
            ES_AUTOVSCROLL |
            ES_AUTOHSCROLL |
            ES_WANTRETURN,
            LEFT_MARGIN,
            EDITOR_TOP,
            1120,
            EDITOR_HEIGHT,
            mainWindow,
            nullptr,
            GetModuleHandleA(
                nullptr
            ),
            nullptr
        );

    HFONT editorFont =
        CreateFontA(
            16,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            ANSI_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            FIXED_PITCH | FF_MODERN,
            "Consolas"
        );

    SendMessage(
        songEditor,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(
            editorFont
        ),
        TRUE
    );

    editBrush =
        CreateSolidBrush(
            EDIT_BACKGROUND
        );

    UpdateTempoDisplay();
    UpdatePitchDisplay();
    UpdateBoostDisplay();

    RebuildTrackControls();
}

// ============================================================
// PAINT MAIN WINDOW
// ============================================================

void PaintMainWindow(
    HWND hwnd,
    HDC dc)
{
    RECT rect;

    GetClientRect(
        hwnd,
        &rect
    );

    HBRUSH backgroundBrush =
        CreateSolidBrush(
            BACKGROUND
        );

    FillRect(
        dc,
        &rect,
        backgroundBrush
    );

    DeleteObject(
        backgroundBrush
    );

    SetBkMode(
        dc,
        TRANSPARENT
    );

    SetTextColor(
        dc,
        RGB(0, 0, 0)
    );

    HFONT titleFont =
        CreateFontA(
            24,
            0,
            0,
            0,
            FW_BOLD,
            FALSE,
            FALSE,
            FALSE,
            ANSI_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            DEFAULT_PITCH,
            "Arial"
        );

    HFONT oldFont =
        static_cast<HFONT>(
            SelectObject(
                dc,
                titleFont
            )
        );

    TextOutA(
        dc,
        15,
        145,
        "MUSIC TRACK EDITOR",
        -1
    );

    SelectObject(
        dc,
        oldFont
    );

    DeleteObject(
        titleFont
    );

    HFONT normalFont =
        static_cast<HFONT>(
            GetStockObject(
                DEFAULT_GUI_FONT
            )
        );

    oldFont =
        static_cast<HFONT>(
            SelectObject(
                dc,
                normalFont
            )
        );

    TextOutA(
        dc,
        LEFT_MARGIN,
        TRACK_TOP - 30 - scrollY,
        "SAVED TRACKS",
        -1
    );

    SelectObject(
        dc,
        oldFont
    );
}

// ============================================================
// MAIN WINDOW PROCEDURE
// ============================================================

LRESULT CALLBACK MainWindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
        // ----------------------------------------------------
        // CREATE
        // ----------------------------------------------------

        case WM_CREATE:
        {
            mainWindow = hwnd;

            CreateMainControls();

            return 0;
        }

        // ----------------------------------------------------
        // PAINT
        // ----------------------------------------------------

        case WM_PAINT:
        {
            PAINTSTRUCT ps;

            HDC dc =
                BeginPaint(
                    hwnd,
                    &ps
                );

            PaintMainWindow(
                hwnd,
                dc
            );

            EndPaint(
                hwnd,
                &ps
            );

            return 0;
        }

        // ----------------------------------------------------
        // EDITOR COLORS
        // ----------------------------------------------------

        case WM_CTLCOLOREDIT:
        {
            HDC dc =
                reinterpret_cast<HDC>(
                    wParam
                );

            SetTextColor(
                dc,
                EDIT_TEXT
            );

            SetBkColor(
                dc,
                EDIT_BACKGROUND
            );

            if (!editBrush)
            {
                editBrush =
                    CreateSolidBrush(
                        EDIT_BACKGROUND
                    );
            }

            return reinterpret_cast<LRESULT>(
                editBrush
            );
        }

        // ----------------------------------------------------
        // STATIC COLORS
        // ----------------------------------------------------

        case WM_CTLCOLORSTATIC:
        {
            HDC dc =
                reinterpret_cast<HDC>(
                    wParam
                );

            SetTextColor(
                dc,
                TEXT_COLOR
            );

            SetBkMode(
                dc,
                TRANSPARENT
            );

            static HBRUSH staticBrush =
                nullptr;

            if (!staticBrush)
            {
                staticBrush =
                    CreateSolidBrush(
                        BACKGROUND
                    );
            }

            return reinterpret_cast<LRESULT>(
                staticBrush
            );
        }

        // ----------------------------------------------------
        // OWNER DRAW
        // ----------------------------------------------------

        case WM_DRAWITEM:
        {
            DrawOwnerButton(
                reinterpret_cast<
                    LPDRAWITEMSTRUCT
                >(lParam)
            );

            return TRUE;
        }

        // ----------------------------------------------------
        // COMMANDS
        // ----------------------------------------------------

        case WM_COMMAND:
        {
            UINT id =
                LOWORD(wParam);

            // ------------------------------------------------
            // PLAY
            // ------------------------------------------------

            if (id == ID_PLAY)
            {
                PlayEditor();
                return 0;
            }

            // ------------------------------------------------
            // PLAY ALL
            // ------------------------------------------------

            if (id == ID_PLAY_ALL)
            {
                StartPlayAll();
                return 0;
            }

            // ------------------------------------------------
            // LOOP
            // ------------------------------------------------

            if (id == ID_LOOP)
            {
                bool newValue =
                    !looping.load();

                looping =
                    newValue;

                SetWindowTextA(
                    loopButton,
                    newValue
                        ? "LOOP: ON"
                        : "LOOP: OFF"
                );

                return 0;
            }

            // ------------------------------------------------
            // BOOST
            // ------------------------------------------------

            if (id == ID_BOOST)
            {
                ChangeBoost();
                return 0;
            }

            // ------------------------------------------------
            // INSTRUMENTS
            // ------------------------------------------------

            if (id == ID_INSTRUMENTS)
            {
                ShowInstrumentSelector();
                return 0;
            }

            // ------------------------------------------------
            // CHORDS
            // ------------------------------------------------

            if (id == ID_CHORDS)
            {
                ShowChordSelector();
                return 0;
            }

            // ------------------------------------------------
            // DRUMS
            // ------------------------------------------------

            if (id == ID_DRUMS)
            {
                ShowDrumSelector();
                return 0;
            }

            // ------------------------------------------------
            // TEMPO
            // ------------------------------------------------

            if (id == ID_TEMPO_MINUS)
            {
                ChangeTempo(
                    -TEMPO_STEP
                );

                return 0;
            }

            if (id == ID_TEMPO_PLUS)
            {
                ChangeTempo(
                    TEMPO_STEP
                );

                return 0;
            }

            // ------------------------------------------------
            // PITCH
            // ------------------------------------------------

            if (id == ID_PITCH_MINUS)
            {
                ChangePitch(
                    -PITCH_STEP
                );

                return 0;
            }

            if (id == ID_PITCH_PLUS)
            {
                ChangePitch(
                    PITCH_STEP
                );

                return 0;
            }

            // ------------------------------------------------
            // SAVE TRACK
            // ------------------------------------------------

            if (id == ID_SAVE_TRACK)
            {
                SaveCurrentTrack();

                RebuildTrackControls();

                return 0;
            }

            // ------------------------------------------------
            // COMBINE + SAVE
            // ------------------------------------------------

            if (id == ID_COMBINE_SAVE)
            {
                SaveCombinedTrackToFile();

                return 0;
            }

            // ------------------------------------------------
            // IMPORT
            // ------------------------------------------------

            if (id == ID_IMPORT_TRACKS)
            {
                ImportTracksFromFile();

                return 0;
            }

            // ------------------------------------------------
            // EXPORT WAV
            // ------------------------------------------------

            if (id == ID_EXPORT_WAV)
            {
                ExportCurrentTrackWav();

                return 0;
            }

            // ------------------------------------------------
            // EXPORT MP3
            // ------------------------------------------------

            if (id == ID_EXPORT_MP3)
            {
                ExportCurrentTrackMp3();

                return 0;
            }

            // ------------------------------------------------
            // EXPORT YOUTUBE MP4
            // ------------------------------------------------

            if (id == ID_EXPORT_MP4)
            {
                ExportCurrentTrackMp4();

                return 0;
            }

            // ------------------------------------------------
            // LOAD TRACK
            // ------------------------------------------------

            if (
                id >= ID_LOAD_TRACK_BASE &&
                id <
                ID_LOAD_TRACK_BASE + 1000)
            {
                size_t index =
                    static_cast<size_t>(
                        id -
                        ID_LOAD_TRACK_BASE
                    );

                LoadTrack(index);

                return 0;
            }

            // ------------------------------------------------
            // PLAY TRACK
            // ------------------------------------------------

            if (
                id >= ID_PLAY_TRACK_BASE &&
                id <
                ID_PLAY_TRACK_BASE + 1000)
            {
                size_t index =
                    static_cast<size_t>(
                        id -
                        ID_PLAY_TRACK_BASE
                    );

                if (
                    index < savedTracks.size() &&
                    !playing)
                {
                    std::thread(
                        PlaySongText,
                        savedTracks[index]
                    ).detach();
                }

                return 0;
            }

            // ------------------------------------------------
            // DELETE TRACK
            // ------------------------------------------------

            if (
                id >= ID_DELETE_TRACK_BASE &&
                id <
                ID_DELETE_TRACK_BASE + 1000)
            {
                size_t index =
                    static_cast<size_t>(
                        id -
                        ID_DELETE_TRACK_BASE
                    );

                DeleteTrack(index);

                RebuildTrackControls();

                return 0;
            }

            // ------------------------------------------------
            // NOTE
            // ------------------------------------------------

            auto noteIt =
                noteCommands.find(id);

            if (
                noteIt !=
                noteCommands.end())
            {
                InsertEditorText(
                    noteIt->second +
                    "\r\n"
                );

                return 0;
            }

            // ------------------------------------------------
            // CHORD
            // ------------------------------------------------

            auto chordIt =
                chordCommands.find(id);

            if (
                chordIt !=
                chordCommands.end())
            {
                InsertEditorText(
                    chordIt->second
                );

                return 0;
            }

            // ------------------------------------------------
            // DRUM
            // ------------------------------------------------

            auto drumIt =
                drumCommands.find(id);

            if (
                drumIt !=
                drumCommands.end())
            {
                InsertEditorText(
                    drumIt->second +
                    "\r\n"
                );

                return 0;
            }

            break;
        }

        // ----------------------------------------------------
        // KEYBOARD
        // ----------------------------------------------------

        case WM_KEYDOWN:
        {
            if (wParam == VK_ESCAPE)
            {
                stopRequested = true;
                return 0;
            }

            break;
        }

        // ----------------------------------------------------
        // MOUSE WHEEL
        // ----------------------------------------------------

        case WM_MOUSEWHEEL:
        {
            int delta =
                GET_WHEEL_DELTA_WPARAM(
                    wParam
                );

            if (delta > 0)
                scrollY -= 40;
            else
                scrollY += 40;

            if (scrollY < 0)
                scrollY = 0;

            int maxScroll =
                std::max(
                    0,
                    contentHeight - 800
                );

            if (scrollY > maxScroll)
                scrollY =
                    maxScroll;

            RebuildTrackControls();

            return 0;
        }

        // ----------------------------------------------------
        // DESTROY
        // ----------------------------------------------------

        case WM_DESTROY:
        {
            stopRequested = true;

            if (editBrush)
            {
                DeleteObject(
                    editBrush
                );

                editBrush = nullptr;
            }

            PostQuitMessage(0);

            return 0;
        }
    }

    return DefWindowProcA(
        hwnd,
        message,
        wParam,
        lParam
    );
}

// ============================================================
// REGISTER WINDOW
// ============================================================

bool RegisterMainWindowClass(
    HINSTANCE instance)
{
    WNDCLASSEXA wc = {};

    wc.cbSize =
        sizeof(WNDCLASSEXA);

    wc.style =
        CS_HREDRAW |
        CS_VREDRAW;

    wc.lpfnWndProc =
        MainWindowProc;

    wc.hInstance =
        instance;

    wc.hCursor =
        LoadCursor(
            nullptr,
            IDC_ARROW
        );

    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(
            GetStockObject(
                NULL_BRUSH
            )
        );

    wc.lpszClassName =
        "MusicTrackEditorWindow";

    return
        RegisterClassExA(
            &wc
        ) != 0;
}

// ============================================================
// DEFAULT SONG
// ============================================================

const char* DEFAULT_SONG =
"TRACK 1 TEMPO 120\r\n"
"LENGTH 8\r\n"
"GUITAR C4 0 1\r\n"
"GUITAR E4 0 1\r\n"
"GUITAR G4 0 1\r\n"
"DRUM KICK 0\r\n"
"DRUM SNARE 2\r\n"
"GUITAR F4 4 1\r\n"
"GUITAR A4 4 1\r\n"
"GUITAR C5 4 1\r\n"
"DRUM KICK 4\r\n";

// ============================================================
// WINMAIN
// ============================================================

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE,
    LPSTR,
    int showCommand)
{
    if (!RegisterMainWindowClass(
        instance))
    {
        MessageBoxA(
            nullptr,
            "Could not register the window.",
            "Error",
            MB_OK | MB_ICONERROR
        );

        return 1;
    }

    mainWindow =
        CreateWindowExA(
            0,
            "MusicTrackEditorWindow",
            "Music Track Editor",
            WS_OVERLAPPEDWINDOW |
            WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1200,
            850,
            nullptr,
            nullptr,
            instance,
            nullptr
        );

    if (!mainWindow)
    {
        MessageBoxA(
            nullptr,
            "Could not create the main window.",
            "Error",
            MB_OK | MB_ICONERROR
        );

        return 1;
    }

    ShowWindow(
        mainWindow,
        showCommand
    );

    UpdateWindow(
        mainWindow
    );

    SetEditorText(
        DEFAULT_SONG
    );

    MSG message;

    while (
        GetMessageA(
            &message,
            nullptr,
            0,
            0
        ) > 0)
    {
        TranslateMessage(
            &message
        );

        DispatchMessageA(
            &message
        );
    }

    return static_cast<int>(
        message.wParam
    );
}
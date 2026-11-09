#include "audio/engine.h"
#include "fixtures.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <numeric>
using namespace piano;
class AudioTests : public QObject {
    Q_OBJECT
  private slots:
    void timelinePauseResumeSpeed() {
        AudioEngine engine;
        Song song;
        song.events = {{0, Kind::NoteOn, 60, 90, Hand::Right}, {1000000, Kind::NoteOff, 60, 0, Hand::Right}};
        engine.setSong(song);
        std::vector<float> block(4800 * 2);
        engine.play();
        engine.render(block.data(), 4800);
        QCOMPARE(engine.status().positionUs, 100000);
        QVERIFY(engine.status().playing);
        engine.pause();
        engine.render(block.data(), 4800);
        QCOMPARE(engine.status().positionUs, 100000);
        QVERIFY(!engine.status().playing);
        engine.setSpeed(2);
        engine.play();
        engine.render(block.data(), 4800);
        QCOMPARE(engine.status().positionUs, 300000);
        engine.stop();
        engine.render(block.data(), 4800);
        QCOMPARE(engine.status().positionUs, 0);
        QVERIFY(!engine.status().playing);
        engine.play();
        for (int n = 0; n < 7; ++n)
            engine.render(block.data(), 4800);
        QVERIFY(!engine.status().playing);
        QVERIFY(engine.status().positionUs >= 1000000);
        engine.play();
        engine.render(block.data(), 4800);
        QVERIFY(engine.status().playing);
        QCOMPARE(engine.status().positionUs, 200000);
    }
    void queueOverflowFailsSilent() {
        AudioEngine engine;
        bool overflow = false;
        for (int n = 0; n < 6000; ++n)
            overflow = !engine.send(Event{0, Kind::NoteOn, 60, 90, Hand::Left}) || overflow;
        QVERIFY(overflow);
        std::array<float, 256> samples{};
        engine.render(samples.data(), 128);
        QVERIFY(engine.status().overflows > 0);
        QVERIFY(!engine.status().playing);
        for (auto sample : samples)
            QCOMPARE(sample, 0.f);
    }
    void samplerAndPanic() {
        QTemporaryDir dir;
        auto path = dir.filePath(QStringLiteral("测试音源.sf2"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(fixture::testSoundfont());
        file.close();
        auto loaded = AudioEngine::loadSoundfont(path);
        QVERIFY2(bool(loaded.font), qPrintable(loaded.error));
        AudioEngine engine;
        engine.setSoundfont(loaded.font);
        QVERIFY(engine.status().ready);
        std::vector<float> samples(4800 * 2);
        engine.send(Event{0, Kind::NoteOn, 69, 110, Hand::Left});
        engine.render(samples.data(), 4800);
        double energy = 0;
        for (float sample : samples) {
            QVERIFY(std::isfinite(sample));
            energy += double(sample) * sample;
        }
        QVERIFY(energy > .01);
        engine.panic();
        engine.render(samples.data(), 4800);
        energy = 0;
        for (float sample : samples)
            energy += double(sample) * sample;
        QVERIFY(energy < .0001);
        engine.setVolume(0);
        engine.send(Event{0, Kind::NoteOn, 69, 110, Hand::Right});
        engine.render(samples.data(), 4800);
        for (float sample : samples)
            QCOMPARE(sample, 0.f);
    }
    void sharedPitchAndPlaybackIsolation() {
        QTemporaryDir dir;
        auto path = dir.filePath("test.sf2");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(fixture::testSoundfont());
        file.close();
        auto loaded = AudioEngine::loadSoundfont(path);
        QVERIFY(bool(loaded.font));
        AudioEngine engine;
        engine.setSoundfont(loaded.font);
        std::vector<float> samples(18000 * 2);
        engine.send(Event{0, Kind::NoteOn, 69, 110, Hand::Left, 30});
        engine.send(Event{0, Kind::NoteOn, 69, 110, Hand::Left, 31});
        engine.render(samples.data(), 4800);
        engine.send(Event{0, Kind::NoteOff, 69, 0, Hand::Left, 30});
        engine.render(samples.data(), 18000);
        double heldEnergy = 0;
        for (size_t i = samples.size() - 4096; i < samples.size(); ++i)
            heldEnergy += double(samples[i]) * samples[i];
        QVERIFY(heldEnergy > .01);
        engine.releaseLive();
        engine.render(samples.data(), 4800);
        Song song;
        song.events = {{0, Kind::NoteOn, 69, 110, Hand::Right}, {1000000, Kind::NoteOff, 69, 0, Hand::Right}};
        engine.setSong(song);
        engine.play();
        engine.render(samples.data(), 4800);
        engine.releaseLive();
        engine.render(samples.data(), 4800);
        double playbackEnergy = 0;
        for (size_t i = 0; i < 9600; ++i)
            playbackEnergy += double(samples[i]) * samples[i];
        QVERIFY(playbackEnergy > .01);
        QVERIFY(engine.status().playing);
        engine.pause();
        engine.render(samples.data(), 4800);
        engine.play();
        engine.render(samples.data(), 4800);
        double resumedEnergy = 0;
        for (size_t i = 0; i < 9600; ++i)
            resumedEnergy += double(samples[i]) * samples[i];
        QVERIFY(resumedEnergy > .01);
        Song pedal;
        pedal.events = {{0, Kind::Sustain, 0, 127, Hand::Right},
                        {0, Kind::NoteOn, 69, 110, Hand::Right},
                        {100000, Kind::NoteOff, 69, 0, Hand::Right},
                        {1000000, Kind::Sustain, 0, 0, Hand::Right}};
        engine.setSong(pedal);
        engine.play();
        samples.resize(48000);
        engine.render(samples.data(), 24000);
        engine.pause();
        engine.render(samples.data(), 4800);
        engine.play();
        engine.render(samples.data(), 4800);
        double pedalEnergy = 0;
        for (size_t i = 0; i < 9600; ++i)
            pedalEnergy += double(samples[i]) * samples[i];
        QVERIFY(pedalEnergy > .01);
    }
    void densePlaybackFailsSilent() {
        AudioEngine engine;
        Song song;
        for (int n = 0; n < 5000; ++n) {
            song.events.push_back({0, Kind::NoteOn, 60, 90, Hand::Left});
            song.events.push_back({0, Kind::NoteOff, 60, 0, Hand::Left});
        }
        engine.setSong(song);
        engine.play();
        std::array<float, 256> block{};
        engine.render(block.data(), 128);
        QVERIFY(!engine.status().playing);
        QCOMPARE(engine.status().overflows, uint64_t(1));
    }
    void invalidSoundfont() {
        auto loaded = AudioEngine::loadSoundfont("/no/such/soundfont.sf2");
        QVERIFY(!loaded.font);
        QVERIFY(!loaded.error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(AudioTests)
#include "audio_tests.moc"

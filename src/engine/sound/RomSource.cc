#include <algorithm>
#include <fstream>
#include <platform/app.hh>
#include <prelude.hh>
#include <utility/endian.hh>

#include <system/Rom.hh>
#include <fluidsynth.h>
#include <ostream>
#include "BinaryReader.hh"

namespace {
  struct Sn64Header {
      // char id[4];
      uint32 game_id; //< Must be 2
      // uint32 _pad0;
      // uint32 _version_id; //< Always 100. Not read or used in game.
      // uint32 _pad1;
      // uint32 _pad2;
      uint32 len1; //< Length of file minus the header size
      // uint32 _pad3;
      uint32 num_inst; //< Number of instruments (31)
      uint16 num_patches; //< Number of patches
      uint16 patch_size; //< sizeof patch struct
      uint16 num_subpatches; //< Number of subpatches
      uint16 subpatch_size; //< sizeof subpatch struct
      uint16 num_sounds; //< Number of sounds
      uint16 sound_size; //< sizeof sound struct
      // uint32 _pad4;
      // uint32 _pad5;

      static Sn64Header from_istream(std::istream& stream)
      {
          Sn64Header d {};

          BinaryReader(stream)
              .magic("SN64")
              .big_endian(d.game_id)
              .ignore_t<uint32>(4)
              .big_endian(d.len1)
              .ignore_t<uint32>()
              .big_endian(d.num_inst,
                          d.num_patches,
                          d.patch_size,
                          d.num_subpatches,
                          d.subpatch_size,
                          d.num_sounds,
                          d.sound_size)
              .ignore_t<uint32>(2)
              .assert_size(56);

          return d;
      }
  };

  struct PatchHeader {
      uint16 length; //< Length (little endian)
      uint16 offset; //< Offset in SN64 file

      static PatchHeader from_istream(std::istream& stream)
      {
          PatchHeader d {};
          BinaryReader(stream)
              .big_endian(d.length, d.offset)
              .assert_size(4);
          return d;
      }
  };

  struct SubpatchHeader {
      uint8 unity_pitch; //< Unknown; used by N64 library
      uint8 attenuation;
      uint8 pan; //< Left/Right panning
      uint8 instrument; //< (Boolean) Treat this as an instrument?
      uint8 root_key; //< Instrument's root key
      uint8 detune; //< Detune key (20120327 villsa - identified)
      uint8 min_note; //< Use this subpatch if note > min_note
      uint8 max_note; //< Use this subpatch if note < max_note
      uint8 pitch_wheel_range_low; //< (20120326 villsa - identified)
      uint8 pitch_wheel_range_high; //< (20120326 villsa - identified)
      uint16 id; //< Sound ID
      int16 attack_time; //< Attack time (fade in)
      // uint8 _unknown0;
      // uint8 _unknown1;
      int16 decay_time; //< Decay time (fade out)
      // uint8 _volume0; //< Volume (unknown purpose)
      // uint8 _volume1; //< Volume (unused?)

      static SubpatchHeader from_istream(std::istream& stream)
      {
          SubpatchHeader d {};
          BinaryReader(stream)
              .big_endian(d.unity_pitch,
                          d.attenuation,
                          d.pan,
                          d.instrument,
                          d.root_key,
                          d.detune,
                          d.min_note,
                          d.max_note,
                          d.pitch_wheel_range_low,
                          d.pitch_wheel_range_high,
                          d.id,
                          d.attack_time)
              .ignore_t<uint8>(2)
              .big_endian(d.decay_time)
              .ignore_t<uint8>(2)
              .assert_size(20);
          return d;
      }
  };

  struct WaveTable {
      uint16 start; //< Start of ROM offset
      uint16 size; //< Size of sound
      // uint16 _pad0;
      uint16 pitch; //< Pitch correction
      uint16 loop_id; //< Index ID for the loop table
      // uint16 _pad1;

      static WaveTable from_istream(std::istream& stream)
      {
          WaveTable d {};
          BinaryReader(stream)
              .big_endian(d.start, d.size)
              .ignore(2)
              .big_endian(d.pitch, d.loop_id)
              .ignore(2)
              .assert_size(12);
          return d;
      }
  };

  struct LoopInfo {
      uint16 num_sounds; //< Sound count (124)
      // uint16 _pad0;
      uint16 num_loops; //< Loop data count (23)
      // uint16 _num_sounds2; //< Sound count again? (124)

      static LoopInfo from_istream(std::istream& stream)
      {
          LoopInfo d {};
          BinaryReader(stream)
              .big_endian(d.num_sounds)
              .ignore(2)
              .big_endian(d.num_loops)
              .ignore(2)
              .assert_size(8);
          return d;
      }
  };

  struct LoopTable {
      uint32 loop_start;
      uint32 loop_end;
      // char _pad0[40]; //< Garbage in rom, but changed/set in game

      static LoopTable from_istream(std::istream& stream)
      {
          LoopTable d {};
          BinaryReader(stream)
              .big_endian(d.loop_start, d.loop_end)
              .ignore(40)
              .assert_size(48);
          return d;
      }
  };

  struct PredictorTable {
      uint32 order; //< Order ID
      uint32 num_predictors; //< Number of predictors
      int16 predictors[128];

      static PredictorTable from_istream(std::istream& stream)
      {
          PredictorTable d {};
          BinaryReader(stream)
              .big_endian(d.order,
                          d.num_predictors,
                          d.predictors)
              .assert_size(264);
          return d;
      }
  };

  struct SseqHeader {
      // char id[4];
      uint32 game_id; //< Must be 2
      // uint32 _pad0;
      uint32 num_entries; //< Number of entries
      // uint32 _pad1;
      // uint32 _pad2;
      uint32 entry_size; //< sizeof(entry) * num_entries
      // uint32 _pad3;

      static constexpr size_t binary_size = 32;

      static SseqHeader from_istream(std::istream& stream)
      {
          SseqHeader d {};
          BinaryReader(stream)
              .magic("SSEQ")
              .big_endian(d.game_id)
              .ignore(4)
              .big_endian(d.num_entries)
              .ignore(8)
              .big_endian(d.entry_size)
              .ignore(4)
              .assert_size(binary_size);
          return d;
      }
  };

  struct EntryHeader {
      uint16 num_tracks; //< Number of tracks
      // uint16 _pad0;
      uint32 length; //< Length of entry
      uint32 offset; //< Offset in SSeq file
      // uint32 _pad1;

      static constexpr size_t binary_size = 16;

      static EntryHeader from_istream(std::istream& stream)
      {
          EntryHeader d {};
          BinaryReader(stream)
              .big_endian(d.num_tracks)
              .ignore(2)
              .big_endian(d.length,
                          d.offset)
              .ignore(4)
              .assert_size(binary_size);
          return d;
      }
  };

  struct TrackHeader {
      uint16 flag; //< Usually0 on sounds, 0x100 on music
      uint16 id; //< Subpatch ID
      // uint16 _pad0;
      uint8 volume; //< Default volume
      uint8 pan; //< Default pan
      // uint16 _pad1;
      uint16 bpm; //< Beats per minute
      uint16 timediv; //< Time division
      uint16 loop; //< (Boolean) 0 if no loop, 1 if yes
      // uint16 _pad2;
      uint16 size; //< Size of MIDI data

      static constexpr size_t binary_size = 20;

      static TrackHeader from_istream(std::istream& stream)
      {
          TrackHeader d {};
          BinaryReader(stream)
              .big_endian(d.flag,
                          d.id)
              .ignore(2)
              .big_endian(d.volume,
                          d.pan)
              .ignore(2)
              .big_endian(d.bpm,
                          d.timediv,
                          d.loop)
              .ignore(2)
              .big_endian(d.size)
              .assert_size(binary_size);
          return d;
      }
  };

  struct MidiTrackHeader {
      char id[4];
      int length;

      static MidiTrackHeader from_istream(std::istream& stream)
      {
          MidiTrackHeader d {};
          BinaryReader(stream)
              .magic("MTrk")
              .big_endian(d.length);
          return d;
      }
  };

  struct MidiHeader {
      char id[4];
      int32 chunk_size;
      int16 type;
      uint16 num_tracks;
      uint16 delta;
      uint32 size;
  };

  std::unique_ptr<PatchHeader[]> patches_;
  std::unique_ptr<SubpatchHeader[]> subpatches_;
  std::vector<std::string> midis_;
  int new_bank_offset_ {};

#if 0
  void decode8(std::istream& in, short* out, int index, const short* pred1, short* last_sample)
  {
      constexpr const short itable[16] = {
          0, 1, 2, 3, 4, 5, 6, 7,
          -8, -7, -6, -5, -4, -3, -2, -1
      };

      std::fill_n(out, 8, 0);

      auto pred2 = pred1 + 8;

      short tmp[8];
      for (size_t i {}; i < 8; i += 2) {
          int c = in.get();
          tmp[i] = itable[c & 0xf] << index;
          tmp[i + 1] = itable[(c >> 4) & 0xf] << index;
      }

      for (int i {}; i < 8; ++i) {
          int64 total {};
          total = (pred1[i] * last_sample[6]);
          total += (pred2[i] * last_sample[7]);

          if (i > 0) {
              for (int j { i - 1 }; j >= 0; --j) {
                  total += tmp[(i - 1) - j] * pred2[j];
              }
          }

          int64 result = ((tmp[i] << 0xb) + total) >> 0xb;
          int16 sample {};

          if (result > 0x7fff) {
              sample = 0x7fff;
          } else if (result < -0x8000) {
              sample = -0x8000;
          } else {
              sample = static_cast<int16>(result);
          }

          out[i] = sample;
      }

      std::copy_n(out, 8, last_sample);
  }

  size_t decode_vadpcm(std::istream& in, short* out, size_t len, const PredictorTable& book)
  {
      short last_sample[8] {};
      int _len = (len / 9) * 9;
      size_t samples {};

      while (_len) {
          int c = in.get();
          int index = (c >> 4) & 0xf;
          int pred = c & 0xf;
          --_len;

          auto pred1 = &book.predictors[pred * 16];

          decode8(in, out, index, pred1, last_sample);
          _len -= 4;

          out += 8;
          decode8(in, out, index, pred1, last_sample);
          _len -= 4;
          out += 8;
          samples += 16;
      }

      return samples;
  }
#endif

  const SubpatchHeader& get_subpatch_by_note(const PatchHeader& patch, int note)
  {
      if (note >= 0) {
          for (size_t i {}; i < patch.length; ++i) {
              auto& s = subpatches_[patch.offset + i];

              if (note >= s.min_note && note <= s.max_note)
                  return s;
          }
      }

      return subpatches_[patch.offset];
  }
  void load_sn64_()
  {
      auto s = rom::sn64();

      /* read header */
      auto sn64 = Sn64Header::from_istream(s);
      auto pos = s.tellg();

      /* read patches */
      patches_ = array_from_istream<PatchHeader>(s, sn64.num_patches);

      pos += sn64.patch_size * sn64.num_patches + sizeof(int);
      s.seekg(pos);

      /* read subpatches */
      subpatches_ = array_from_istream<SubpatchHeader>(s, sn64.num_subpatches);

      pos += sn64.subpatch_size * sn64.num_subpatches + sizeof(int);
      s.seekg(pos);

      /* find where non-instruments begin */
      for (size_t i {}; i < sn64.num_patches; ++i) {
          if (!subpatches_[patches_[i].offset].instrument) {
              new_bank_offset_ = i;
              break;
          }
      }

      /* read waves */
      auto waves = array_from_istream<WaveTable>(s, sn64.num_sounds);
      pos += sn64.sound_size * sn64.num_sounds;
      s.seekg(pos);

      /* read loop info */
      auto loop_info = LoopInfo::from_istream(s);

      /* read loop table */
      auto loop_table = array_from_istream<LoopTable>(s, loop_info.num_loops);
      pos += 2 * (loop_info.num_loops + 1) + loop_info.num_loops;
      s.seekg(pos);

      /* read predictors */
      auto predictors = array_from_istream<PredictorTable>(s, sn64.num_sounds);

      /* read pcm */
  }

  enum struct midi {
      program_change = 0x07,
      pitch_bend     = 0x09,
      unknown1       = 0x0b,
      global_volume  = 0x0c,
      global_panning = 0x0d,
      sustain_pedal  = 0x0e,
      play_note      = 0x11,
      stop_note      = 0x12,
      goto_loop      = 0x20,
      end_marker     = 0x22,
      set_loop       = 0x23
  };

  class MidiWriter {
      std::ostream& out_;
      char chan_ {};

      void chan_prefix()
      {
          out_.put(0xb0_i8 | chan_);
      }

  public:
      MidiWriter(std::ostream& out, char chan):
          out_(out),
          chan_(chan) {}

      void bank_select(char bank)
      {
          chan_prefix();
          out_.put(0);
          out_.put(bank);
      }

      void program_change(char program)
      {
          out_.put(0xc0_i8 | chan_);
          out_.put(program);
      }

      void set_volume(char volume)
      {
          chan_prefix();
          out_.put(0x07);
          out_.put(volume);
      }

      void set_pan(char pan)
      {
          chan_prefix();
          out_.put(0xa_i8);
          out_.put(pan);
      }

      void set_loop_position(char loop)
      {
          out_.write("\xff\x7f\x02\x00", 4);
          out_.put(loop);
      }

      void jump_loop_position(int loop)
      {
          out_.write("\xff\x7f\x04\x00", 4);
      }

      void registered_parameter_number(uint16_t num)
      {
          chan_prefix();
          out_.put(0x65);
          out_.write(reinterpret_cast<char*>(&num), sizeof(num));

          chan_prefix();
          out_.put(0x64);
          out_.write(reinterpret_cast<char*>(&num), sizeof(num));
      }

      void data_entry(char value)
      {
          chan_prefix();
          out_.put(0x06);
          out_.put(value);
          out_.put(0);

          chan_prefix();
          out_.put(0x26);
          out_.put(0);
          out_.put(0);
      }
  };

  namespace midi_meta_events {
    /* Format: FF 51 03 tttttt */
    void set_tempo(std::ostream& s, int tempo)
    {
        const char *tempo_str = reinterpret_cast<char*>(&tempo);
        s.write("\x00\xff\x51\x03", 4);
        s.put(tempo_str[2]);
        s.put(tempo_str[1]);
        s.put(tempo_str[0]);
    }

    void end_marker(std::ostream& s)
    {
        s.write("\xff\x2f", 2);
    }
  }

  void read_midi_(std::string& midi_data, const TrackHeader& track, const std::string& s, char chan, size_t index,
                  const PatchHeader &patch)
  {
      char tmp[4];
      int pitchbend {};
      int bendrange {};
      int note {};

      auto& inst = subpatches_[patch.offset];

      //header.delta = big_endian(track.timediv);
      //header.type = big_endian(inst.instrument);

      /* Avoid percussion channels (default as 9) */
      if (chan >= 9)
          chan++;

      std::ostringstream ss;
      MidiWriter w { ss, chan };

      /* Set initial tempo */
      if (!chan) {
          midi_meta_events::set_tempo(ss, 60'000'000 / track.bpm);
      }

      /* Set initial bank */
      if (!inst.instrument) {
          ss.put(0);
          w.bank_select(1);
      }

      ss.put(0);
      w.program_change(track.id - (track.id >= new_bank_offset_ ? new_bank_offset_ : 0));
      ss.put(0);
      w.set_volume(track.volume);
      ss.put(0);
      w.set_pan(track.pan);

      auto s2 = s;
      s2.resize(s2.size() + 1000);
      const char* m = s2.c_str();
      for (;;) {
          do {
              ss.put(*m);
          } while (*m++ < 0);

          if (*m == 0x22) {
              ss.write("\xff\x2f", 2);
              break;
          }

          switch (static_cast<midi>(*m)) {
          case midi::end_marker: // 0x22
              midi_meta_events::end_marker(ss);
              break;

          case midi::program_change: // 0x07
              m++;
              tmp[0] = *m++;
              w.program_change(tmp[0] - (tmp[0] >= new_bank_offset_ ? new_bank_offset_ : 0));
              m++;
              break;

          case midi::pitch_bend: // 0x09
              if (inst.instrument && inst.pitch_wheel_range_high != bendrange) {
                  bendrange = inst.pitch_wheel_range_high;

                  w.registered_parameter_number(0);
                  w.data_entry(bendrange);
                  w.registered_parameter_number(0x7f00);
              }

              ss.put(0xe0_u8 | chan);
              m++;

              tmp[0] = *m++;
              tmp[1] = *m++;
              pitchbend = tmp[0] | (tmp[1] << 8);
              pitchbend += 0x2000;

              if (pitchbend > 0x3fff)
                  pitchbend = 0x3fff;

              if (pitchbend < 0)
                  pitchbend = 0;

              pitchbend *= 2;

              ss.put(pitchbend & 0xff);
              ss.put(pitchbend >> 8);
              break;

          case midi::unknown1: {
              m += 2;
              ss.write("\xff\x07\x13", 3);
              ss.write("UNKNOWN EVENT: 0x11", 20);
          }
              break;

          case midi::global_volume: // 0x0c
              m++;
              w.set_volume(*m++);
              break;

          case midi::global_panning: // 0x0d
              /* We're skipping the first byte for whatever reason */
              m++;
              ss.put(0xb0_u8 | chan);
              ss.put(0x0a);
              ss.put(*m++);
              break;

          case midi::sustain_pedal: // 0x0e
              /* We're skipping the first byte for whatever reason */
              m++;
              ss.put(0xb0_u8 | chan);
              ss.put(0x40);
              ss.put(*m++);
              break;

          case midi::play_note:
              /* We're skipping the first byte for whatever reason */
              m++;
              ss.put(0x90_u8 | chan);
              tmp[1] = *m++;
              ss.put(tmp[1]);
              ss.put(*m++);
              note = tmp[1];
              break;

          case midi::stop_note:
              /* We're skipping the first byte for whatever reason */
              m++;
              ss.put(0x90_u8 | chan);
              ss.put(*m++);
              ss.put(0);
              break;

          case midi::goto_loop:
              ss.write("\xff\x7f\x04\x00", 4);
              ss.put(*m++);
              ss.put(*m++);
              ss.put(*m++);
              break;

          case midi::set_loop:
              ss.write("\xff\x7f\x02\x00", 4);
              ss.put(*m++);
              break;

          default:
              fatal("Unknown MIDI event: {}", static_cast<int>(*m));
              break;
          }

          inst = get_subpatch_by_note(patch, note);
      }

      ss.put(0);

      const auto &data = ss.str();
      size_t size = data.size();
      midi_data.append("MTrk", 4);
      midi_data.push_back((size >> 24) & 0xff);
      midi_data.push_back((size >> 16) & 0xff);
      midi_data.push_back((size >> 8) & 0xff);
      midi_data.push_back(size & 0xff);

      midi_data.append(data);
  }

  void load_sseq_()
  {
      auto s = rom::sseq();

      /* process header */
      auto sseq = SseqHeader::from_istream(s);

      /* process entries */
      auto entries = array_from_istream<EntryHeader>(s, sseq.num_entries);
      assert(EntryHeader::binary_size * sseq.num_entries == sseq.entry_size);

      fmt::print("!SSEQ: num_entries {}, entry_size {}\n", sseq.num_entries, sseq.entry_size);

      auto track_table = static_cast<size_t>(s.tellg());
      for (size_t i {}; i < sseq.num_entries; ++i) {
          auto& entry = entries[i];
          s.seekg(track_table + entry.offset);

          midis_.emplace_back();
          auto& midi_data = midis_.back();
          midi_data.append("MThd\0\0\0\x06\0\0\0\0\0\0", 14);

          constexpr size_t type_pos = 8;
          constexpr size_t num_tracks_pos = 10;
          constexpr size_t timediv_pos = 12;

          midi_data[num_tracks_pos] = (entry.num_tracks >> 8) & 0xff;
          midi_data[num_tracks_pos + 1] = entry.num_tracks & 0xff;

          for (size_t j {}; j < entry.num_tracks; ++j) {
              auto track = TrackHeader::from_istream(s);

              const auto &patch = patches_[track.id];
              const auto &inst = subpatches_[patch.offset];
              midi_data[type_pos] = (inst.instrument >> 8) & 0xff;
              midi_data[type_pos + 1] = inst.instrument & 0xff;
              midi_data[timediv_pos] = (track.timediv >> 8) & 0xff;
              midi_data[timediv_pos + 1] = track.timediv & 0xff;

              if (track.loop) {
                  s.ignore(4);
              }

              std::string str;
              str.resize(track.size);
              s.read(&str[0], track.size);

              std::istringstream ss(str);

              if (!(track.flag & 0x100) && entry.num_tracks > 1)
                  fatal("Bad track {} offset [entry {:03d}], {}", j, i, static_cast<size_t>(s.tellg()));

              read_midi_(midi_data, track, str, j, i, patches_[track.id]);
          }
      }
  }
}

std::string get_midi(size_t midi)
{
    return midis_.at(midi);
}

fluid_sfont_t* rom_sfont()
{
    return new fluid_sfont_t {
        .data = nullptr,
            .id = 0,
            .free = [](fluid_sfont_t* sf)
            {
                delete sf;
                return 0;
            },
            .get_name = [](fluid_sfont_t*)
                 {
                     static auto name = "Doom64EX RomSource"_sv;
                     auto copy_name = new char[name.length() + 1];
                     std::copy_n(name.data(), name.length(), copy_name);
                     copy_name[name.length()] = 0;
                     return copy_name;
                 },
                 .get_preset = [](fluid_sfont_t*, uint32 bank, uint32 prenum) -> fluid_preset_t* { return nullptr; },
                      .iteration_start = [](fluid_sfont_t*) {},
                           .iteration_next = [](fluid_sfont_t*, fluid_preset_t*) { return 0; }
    };
}

fluid_sfloader_t* rom_soundfont()
{
    load_sn64_();
    load_sseq_();

    return new fluid_sfloader_t {
        .data = nullptr,
            .free = [](fluid_sfloader_t* sf)
            {
                delete sf;
                return 0;
            },
            .load = [](fluid_sfloader_t*, const char* fname) -> fluid_sfont_t*
                 {
                     fmt::print("Loading font: {}\n", fname);
                     if ("DOOM64.ROM"_sv == fname) {
                         fmt::print("Trying to load DOOM64ROM Soundfont");
                         return rom_sfont();
                     }
                     return nullptr;
                 }
    };
}
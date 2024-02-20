#!/usr/bin/ruby

require "shellwords"
require "open3"
require "pathname"
require "json"
require "toml-rb"
require "logger"
require "optparse"

##### Globals
$logger = Logger.new $stderr, level: :info
$req_file = "_test_req_packet"
$res_file = "_test_res_packet"
$skey = "_test_sk"
$bkey = "_test_bk"
$has_any_error = false
$IYOKAN_ARGS = []
$SKIP_PREFACE = false
$REPEAT = 1

##### utility #####

def quote(str, prefix = "> ")
  prefix + str.gsub("\n", "\n#{prefix}")
end

class Array
  def resize(n, init = 0)
    self + [init] * [n - size, 0].max
  end
end

def toml2packet(toml)
  def normalize_entry(entry)
    (entry || []).map { |v|
      [v["name"], {
        bytes: v["bytes"].resize((v["size"] / 8.0).ceil),
        size: v["size"],
      }]
    }.to_h
  end

  {
    cycles: toml["cycles"] || -1,
    ram: normalize_entry(toml["ram"]),
    rom: normalize_entry(toml["rom"]),
    bits: normalize_entry(toml["bits"]),
  }
end

##### assert #####

def assert_equal(got, expected)
  raise "Assert failed: assert_equal #{expected.inspect}\n#{quote(got.to_s)}" unless got == expected
end

def assert_include(src, val)
  raise "Assert failed: assert_include #{val.inspect}\n#{quote(src.to_s)}" unless src.include?(val)
end

def assert_regex(str, reg)
  raise "Assert failed: assert_regex #{reg.inspect}\n#{quote(str)}" unless str =~ reg
end

def assert_equal_packet(got, expected)
  assert_equal toml2packet(got), toml2packet(expected)
end

#### Run

def run_command(command, args = [])
  path = Pathname.new($path) / command
  cmd = "#{Shellwords.join([path.to_s] + args)}"
  res, err, code = Open3.capture3(cmd)
  raise "Unexpected status code: #{code}\n#{quote(cmd)}\n#{quote(res)}\n#{quote(err)}" if code != 0
  res
end

def run_iyokan(args)
  run_command "./iyokan", (args + $IYOKAN_ARGS)
end

def run_iyokan_packet(args)
  run_command "./iyokan-packet", args
end

##### Parse command-line arguments
opt = OptionParser.new
opt.on("--skip-preface") { |v| $SKIP_PREFACE = true }
opt.on("--iyokan-arg ARG") { |v| $IYOKAN_ARGS.push v }
opt.on("--repeat NUM") { |v| $REPEAT = v.to_i }
opt.banner += " PATH [fast|plain|tfhe|cufhe|TEST-NAME]"
opt.parse! ARGV
unless ARGV.size >= 2
  puts opt.to_s
  exit 1
end
$path = ARGV.shift
$logger.info "$path == #{$path}"
$logger.info "$IYOKAN_ARGS == #{$IYOKAN_ARGS}"

##### test0 #####

if $SKIP_PREFACE
  $logger.info "Skip test0."
else
  $logger.info "test0 running..."
  run_command "./test0"
  $logger.info "test0 done."
end

##### prepare #####

$logger.info "Preparing skey and evalkey..."
run_iyokan_packet ["genkey", "--type", "tfhepp", "--out", $skey] unless File.exist? $skey
run_iyokan_packet ["genevalkey", "--in", $skey, "--out", $bkey] unless File.exist? $bkey
$logger.info "Preparing skey and evalkey done."

##### method toml2packet #####

def test_method_toml2packet(in_file, expected)
  toml = TomlRB.load_file(in_file)
  got = toml2packet toml
  assert_equal got, expected
end

if $SKIP_PREFACE
  $logger.info "Skip test of method toml2packet."
else
  $logger.info "Testing toml2packet..."
  test_method_toml2packet "test/in/test03.in", {
                            cycles: -1,
                            ram: {},
                            rom: {},
                            bits: {
                              "hoge" => { size: 3, bytes: [5] },
                              "piyo" => { size: 3, bytes: [0] },
                            },
                          }
  $logger.info "Testing toml2packet done."
end

##### iyokan-packet #####

def test_iyokan_packet_e2e(in_file)
  pkt = $req_file

  run_iyokan_packet ["toml2packet", "--in", in_file, "--out", pkt]
  run_iyokan_packet ["enc", "--key", $skey, "--in", pkt, "--out", pkt]
  run_iyokan_packet ["dec", "--key", $skey, "--in", pkt, "--out", pkt]
  r = run_iyokan_packet ["packet2toml", "--in", pkt]

  got = TomlRB.parse(r)
  expected = TomlRB.load_file(in_file)
  assert_equal_packet got, expected
end

if $SKIP_PREFACE
  $logger.info "Skip test of iyokan_packet toml2packet."
else
  $logger.info "Testing toml2packet running..."
  test_iyokan_packet_e2e "test/in/test00.in"
  test_iyokan_packet_e2e "test/out/test00-diamond.out"
  test_iyokan_packet_e2e "test/in/test03.in"
  $logger.info "Testing toml2packet done."
end

if $SKIP_PREFACE
  $logger.info "Skip test of iyokan-packet convert-plain."
else
  $logger.info "Testing iyokan-packet convert-plain running..."
  run_iyokan_packet ["toml2packet", "--in", "test/in/test00.in", "--out", "_test_plain_pkt0"]
  run_iyokan_packet ["toml2packet", "--in", "test/out/test08.out", "--out", "_test_plain_pkt1"]
  run_iyokan_packet ["toml2packet", "--in", "test/in/test03.in", "--out", "_test_plain_pkt2"]
  run_iyokan_packet ["convert-plain",
                     "--in",
                     "a", "_test_plain_pkt0",
                     "b", "_test_plain_pkt1",
                     "c", "_test_plain_pkt2",
                     "--",
                     "--out", "_test_plain_pkt2",
                     "rom.foo = a.rom",
                     "ram.bar = a.ramB",
                     "bits.baz = b.rdata",
                     "ram.hoge = b.target",
                     "bits.piyo = c.hoge"]
  r = run_iyokan_packet ["packet2toml", "--in", "_test_plain_pkt2"]
  got = TomlRB.parse(r)
  expected = TomlRB.load_file("test/in/test17.in")
  assert_equal_packet got, expected
  $logger.info "Testing iyokan-packet convert-plain done."
end

if $SKIP_PREFACE
  $logger.info "Skip test of iyokan-packet convert."
else
  $logger.info "Testing iyokan-packet convert running..."
  run_iyokan_packet ["toml2packet", "--in", "test/in/test00.in", "--out", "_test_plain_pkt0"]
  run_iyokan_packet ["toml2packet", "--in", "test/out/test08.out", "--out", "_test_plain_pkt1"]
  run_iyokan_packet ["toml2packet", "--in", "test/in/test03.in", "--out", "_test_plain_pkt2"]
  run_iyokan_packet ["enc", "--key", $skey, "--in", "_test_plain_pkt0", "--out", "_test_pkt0"]
  run_iyokan_packet ["enc", "--key", $skey, "--in", "_test_plain_pkt1", "--out", "_test_pkt1"]
  run_iyokan_packet ["enc", "--key", $skey, "--in", "_test_plain_pkt2", "--out", "_test_pkt2"]
  run_iyokan_packet ["convert",
                     "--in",
                     "a", "_test_pkt0",
                     "b", "_test_pkt1",
                     "c", "_test_pkt2",
                     "--",
                     "--out", "_test_pkt2",
                     "rom.foo = a.rom",
                     "ram.bar = a.ramB",
                     "bits.baz = b.rdata",
                     "ram.hoge = b.target",
                     "bits.piyo = c.hoge"]
  run_iyokan_packet ["dec", "--key", $skey, "--in", "_test_pkt2", "--out", "_test_plain_pkt2"]
  r = run_iyokan_packet ["packet2toml", "--in", "_test_plain_pkt2"]
  got = TomlRB.parse(r)
  expected = TomlRB.load_file("test/in/test17.in")
  assert_equal_packet got, expected
  $logger.info "Testing iyokan-packet convert done."
end

##### iyokan #####

class TestRunner
  def initialize(tests)
    @tests = tests
  end

  def run
    $REPEAT.times do |i|
      $logger.info "Iteration #{i + 1}/#{$REPEAT}"
      @tests.each do |test|
        $logger.info "Test #{test[:name]} running..."
        start = Time.now
        begin
          test[:body].call
          $logger.info "Test #{test[:name]} done. (#{Time.now - start} sec.)"
        rescue
          $logger.fatal "Test #{test[:name]} failed! (#{Time.now - start} sec.)"
          raise $!  # re-throw the exception
        end
      end
    end
  end
end

class TestRegisterer
  def initialize
    @tests = {}
  end

  def add(name, tags, &body)
    tags = (tags + [name.to_sym]).uniq
    @tests[name] = {
      name: name,
      tags: tags,
      body: body,
    }
    $logger.info "Test #{name} (#{tags}) added."
  end

  def add_plain(name, blueprint, in_file, out_file,
                tags: [], ncycles: -1, iyokan_args: [], after_assert: nil)
    name = "plain-" + name
    add(name, tags + [:plain, :fast]) do
      run_iyokan_packet ["toml2packet",
                         "--in", in_file,
                         "--out", $req_file]
      run_iyokan (["plain",
                   "--blueprint", blueprint,
                   "-i", $req_file,
                   "-o", $res_file,
                   "-c", ncycles] + iyokan_args)
      r = run_iyokan_packet ["packet2toml", "--in", $res_file]

      got = TomlRB.parse r
      expected = TomlRB.load_file out_file
      assert_equal_packet got, expected

      after_assert.call unless after_assert.nil?
    end
  end

  def add_tfhe(name, blueprint, in_file, out_file,
               tags: [], ncycles:, iyokan_args: [], after_assert: nil)
    add("tfhe-" + name, tags + [:tfhe]) do
      run_iyokan_packet ["toml2packet",
                         "--in", in_file,
                         "--out", $req_file]
      run_iyokan_packet ["enc",
                         "--key", $skey,
                         "--in", $req_file,
                         "--out", $req_file]
      run_iyokan (["tfhe",
                   "--blueprint", blueprint,
                   "--evalkey", $bkey,
                   "-i", $req_file,
                   "-o", $res_file,
                   "-c", ncycles] + iyokan_args)
      run_iyokan_packet ["dec",
                         "--key", $skey,
                         "--in", $res_file,
                         "--out", $res_file]
      r = run_iyokan_packet ["packet2toml", "--in", $res_file]

      got = TomlRB.parse r
      expected = TomlRB.load_file out_file
      assert_equal_packet got, expected

      after_assert.call unless after_assert.nil?
    end
  end

  def add_cufhe(name, blueprint, in_file, out_file,
                tags: [], ncycles:, iyokan_args: [], after_assert: nil)
    add("cufhe-" + name, tags + [:cufhe]) do
      run_iyokan_packet ["toml2packet",
                         "--in", in_file,
                         "--out", $req_file]
      run_iyokan_packet ["enc",
                         "--key", $skey,
                         "--in", $req_file,
                         "--out", $req_file]
      run_iyokan (["tfhe",
                   "--enable-gpu",
                   "--blueprint", blueprint,
                   "--bkey", $bkey,
                   "-i", $req_file,
                   "-o", $res_file,
                   "-c", ncycles] + iyokan_args)
      run_iyokan_packet ["dec",
                         "--key", $skey,
                         "--in", $res_file,
                         "--out", $res_file]
      r = run_iyokan_packet ["packet2toml", "--in", $res_file]

      got = TomlRB.parse r
      expected = TomlRB.load_file out_file
      assert_equal_packet got, expected

      after_assert.call unless after_assert.nil?
    end
  end

  def add_in_out(name, blueprint, in_file, out_file,
                 ncycles:, set_plain_ncycles: false,
                 plain_tags: [], tfhe_tags: [], cufhe_tags: [],
                 plain_iyokan_args: [], tfhe_iyokan_args: [], cufhe_iyokan_args: [],
                 &after_assert)
    unless plain_tags.nil?
      add_plain name, blueprint, in_file, out_file,
                tags: plain_tags,
                ncycles: (set_plain_ncycles ? ncycles : -1),
                iyokan_args: plain_iyokan_args,
                after_assert: after_assert
    end
    unless tfhe_tags.nil?
      add_tfhe name, blueprint, in_file, out_file,
               tags: tfhe_tags,
               ncycles: ncycles,
               iyokan_args: tfhe_iyokan_args,
               after_assert: after_assert
    end
    unless cufhe_tags.nil?
      add_cufhe name, blueprint, in_file, out_file,
                tags: cufhe_tags,
                ncycles: ncycles,
                iyokan_args: cufhe_iyokan_args,
                after_assert: after_assert
    end
  end

  def get_runner(tags: [])
    tests = @tests.select { |name, test|
      tags.all? { |tag| test[:tags].include? tag }
    }.values.shuffle
    $logger.info "[#{tests.size} TESTS SELECTED (#{tags})] #{tests.map { |t| t[:name] }.join(", ")}"
    TestRunner.new tests
  end
end

reg = TestRegisterer.new

reg.add_in_out "cahp-diamond-00", "test/config-toml/cahp-diamond.toml",
               "test/in/test00.in", "test/out/test00-diamond.out", ncycles: 8
reg.add_in_out "cahp-emerald-00", "test/config-toml/cahp-emerald.toml",
               "test/in/test00.in", "test/out/test00-emerald.out", ncycles: 6
reg.add_in_out "cahp-ruby-09", "test/config-toml/cahp-ruby.toml",
               "test/in/test09.in", "test/out/test09-ruby.out", ncycles: 7
reg.add_in_out "cahp-pearl-09", "test/config-toml/cahp-pearl.toml",
               "test/in/test09.in", "test/out/test09-pearl.out", ncycles: 3, cufhe_tags: [:fast]

reg.add_in_out "cahp-diamond-mux-00", "test/config-toml/cahp-diamond-mux.toml",
               "test/in/test00.in", "test/out/test00-diamond.out", ncycles: 8
reg.add_in_out "cahp-emerald-mux-00", "test/config-toml/cahp-emerald-mux.toml",
               "test/in/test00.in", "test/out/test00-emerald.out", ncycles: 6
reg.add_in_out "cahp-ruby-mux-09", "test/config-toml/cahp-ruby-mux.toml",
               "test/in/test09.in", "test/out/test09-ruby.out", ncycles: 7
reg.add_in_out "cahp-pearl-mux-09", "test/config-toml/cahp-pearl-mux.toml",
               "test/in/test09.in", "test/out/test09-pearl.out", ncycles: 3

reg.add_in_out "cahp-diamond-01", "test/config-toml/cahp-diamond.toml",
               "test/in/test01.in", "test/out/test01-diamond.out", ncycles: 346, tfhe_tags: nil
reg.add_in_out "cahp-emerald-01", "test/config-toml/cahp-emerald.toml",
               "test/in/test01.in", "test/out/test01-emerald.out", ncycles: 261, tfhe_tags: nil
reg.add_in_out "cahp-ruby-10", "test/config-toml/cahp-ruby.toml",
               "test/in/test10.in", "test/out/test10-ruby.out", ncycles: 362, tfhe_tags: nil
reg.add_in_out "cahp-pearl-10", "test/config-toml/cahp-pearl.toml",
               "test/in/test10.in", "test/out/test10-pearl.out", ncycles: 264, tfhe_tags: nil

reg.add_in_out "cahp-diamond-mux-01", "test/config-toml/cahp-diamond-mux.toml",
               "test/in/test01.in", "test/out/test01-diamond.out", ncycles: 346, tfhe_tags: nil
reg.add_in_out "cahp-emerald-mux-01", "test/config-toml/cahp-emerald-mux.toml",
               "test/in/test01.in", "test/out/test01-emerald.out", ncycles: 261, tfhe_tags: nil
reg.add_in_out "cahp-ruby-mux-10", "test/config-toml/cahp-ruby-mux.toml",
               "test/in/test10.in", "test/out/test10-ruby.out", ncycles: 362, tfhe_tags: nil
reg.add_in_out "cahp-pearl-mux-10", "test/config-toml/cahp-pearl-mux.toml",
               "test/in/test10.in", "test/out/test10-pearl.out", ncycles: 264, tfhe_tags: nil

reg.add_in_out "cahp-ruby-mux-1KiB-11", "test/config-toml/cahp-ruby-mux-1KiB.toml",
               "test/in/test11.in", "test/out/test11.out", ncycles: 7

reg.add_in_out "const-4bit-22", "test/config-toml/const-4bit.toml",
               "test/in/test22.in", "test/out/test22.out", ncycles: 1, set_plain_ncycles: true
reg.add_in_out "addr-4bit-04", "test/config-toml/addr-4bit.toml",
               "test/in/test04.in", "test/out/test04.out", ncycles: 1, set_plain_ncycles: true
reg.add_in_out "pass-addr-pass-4bit-04", "test/config-toml/pass-addr-pass-4bit.toml",
               "test/in/test04.in", "test/out/test04.out", ncycles: 1, set_plain_ncycles: true
reg.add_in_out "addr-register-4bit-16", "test/config-toml/addr-register-4bit.toml",
               "test/in/test16.in", "test/out/test16.out", ncycles: 3, set_plain_ncycles: true
reg.add_in_out "div-8bit-05", "test/config-toml/div-8bit.toml",
               "test/in/test05.in", "test/out/test05.out", ncycles: 1, set_plain_ncycles: true
reg.add_in_out "ram-addr8bit-06", "test/config-toml/ram-addr8bit.toml",
               "test/in/test06.in", "test/out/test06.out", ncycles: 16, set_plain_ncycles: true
reg.add_in_out "ram-addr9bit-07", "test/config-toml/ram-addr9bit.toml",
               "test/in/test07.in", "test/out/test07.out", ncycles: 16, set_plain_ncycles: true
reg.add_in_out "mux-ram-addr8bit-06", "test/config-toml/mux-ram-addr8bit.toml",
               "test/in/test06.in", "test/out/test06.out", ncycles: 16, set_plain_ncycles: true
reg.add_in_out "mux-ram-addr9bit-07", "test/config-toml/mux-ram-addr9bit.toml",
               "test/in/test07.in", "test/out/test07.out", ncycles: 16, set_plain_ncycles: true
reg.add_in_out "ram-8-16-16-08", "test/config-toml/ram-8-16-16.toml",
               "test/in/test08.in", "test/out/test08.out", ncycles: 8, set_plain_ncycles: true
reg.add_in_out "mux-ram-8-16-16-08", "test/config-toml/mux-ram-8-16-16.toml",
               "test/in/test08.in", "test/out/test08.out", ncycles: 8, set_plain_ncycles: true
reg.add_in_out "rom-7-32-12", "test/config-toml/rom-7-32.toml",
               "test/in/test12.in", "test/out/test12.out", ncycles: 1, set_plain_ncycles: true
reg.add_in_out "rom-4-8-15", "test/config-toml/rom-4-8.toml",
               "test/in/test15.in", "test/out/test15.out", ncycles: 1, set_plain_ncycles: true
reg.add_in_out "counter-4bit-13", "test/config-toml/counter-4bit.toml",
               "test/in/test13.in", "test/out/test13.out", ncycles: 3, set_plain_ncycles: true
reg.add_in_out "cahp-ruby-14", "test/config-toml/cahp-ruby.toml",
               "test/in/test14.in", "test/out/test14.out", ncycles: 20, set_plain_ncycles: true, tfhe_tags: nil, cufhe_tags: nil
reg.add_in_out "cahp-ruby-iyokanl1-09", "test/config-toml/cahp-ruby-iyokanl1.toml",
               "test/in/test09.in", "test/out/test09-ruby.out", ncycles: -1, tfhe_tags: nil, cufhe_tags: nil
#reg.add_in_out("register-init-4bit-18", "test/config-toml/register-init-4bit.toml",
#               "test/in/test18.in", "test/out/test18.out",
#               ncycles: 1, set_plain_ncycles: true,
#               plain_iyokan_args: ["--skip-reset"],
#               tfhe_iyokan_args: ["--skip-reset", "--secret-key", "_test_sk"],
#               cufhe_iyokan_args: ["--skip-reset", "--secret-key", "_test_sk"])
#reg.add_in_out("register-init-4bit-19", "test/config-toml/register-init-4bit.toml",
#               "test/in/test19.in", "test/out/test19.out",
#               ncycles: 2, set_plain_ncycles: true,
#               plain_iyokan_args: ["--skip-reset"],
#               tfhe_iyokan_args: ["--skip-reset", "--secret-key", "_test_sk"],
#               cufhe_iyokan_args: ["--skip-reset", "--secret-key", "_test_sk"])

reg.add_in_out "big-mult-21", "test/config-toml/big-mult.toml",
               "test/in/test21.in", "test/out/test21.out", ncycles: 1, set_plain_ncycles: true, tfhe_tags: nil

reg.add_in_out("cahp-diamond-dump-prefix-00", "test/config-toml/cahp-diamond.toml",
               "test/in/test00.in", "test/out/test00-diamond.out",
               ncycles: 8,
               plain_iyokan_args: ["--dump-prefix", "_test_dump"],
               tfhe_iyokan_args: ["--dump-prefix", "_test_dump", "--secret-key", "_test_sk"],
               cufhe_iyokan_args: ["--dump-prefix", "_test_dump", "--secret-key", "_test_sk"]) do
  r = run_iyokan_packet ["packet2toml", "--in", "_test_dump-7"]
  toml = TomlRB.parse(r)
  assert_equal toml["cycles"].to_i, 7
  assert_include toml["bits"], { "bytes" => [0], "size" => 1, "name" => "finflag" }
  assert_include toml["bits"], { "bytes" => [42, 0], "size" => 16, "name" => "reg_x0" }
end

reg.add("plain-addr-addr-4bit-20", [:plain, :fast]) do
  in_file = "test/in/test20.in"
  out_file = "test/out/test20.out"

  run_iyokan_packet ["toml2packet", "--in", in_file, "--out", $req_file]
  run_iyokan ["plain",
              "--blueprint", "test/config-toml/addr-4bit.toml",
              "-i", $req_file,
              "-o", $res_file,
              "-c", 1]
  run_iyokan_packet ["convert-plain",
                     "-o", $req_file,
                     "-i", "a", $res_file,
                     "--",
                     "bits.A = a.out",
                     "bits.B = a.out"]
  run_iyokan ["plain",
              "--blueprint", "test/config-toml/addr-4bit.toml",
              "-i", $req_file,
              "-o", $res_file,
              "-c", 1]
  r = run_iyokan_packet ["packet2toml", "--in", $res_file]

  got = TomlRB.parse r
  expected = TomlRB.load_file out_file
  assert_equal_packet got, expected
end

reg.add("tfhe-addr-addr-4bit-20", [:tfhe]) do
  in_file = "test/in/test20.in"
  out_file = "test/out/test20.out"

  run_iyokan_packet ["toml2packet", "--in", in_file, "--out", $req_file]
  run_iyokan_packet ["enc", "--key", $skey, "--in", $req_file, "--out", $req_file]
  run_iyokan ["tfhe",
              "--blueprint", "test/config-toml/addr-4bit.toml",
              "--bkey", $bkey,
              "-i", $req_file,
              "-o", $res_file,
              "-c", 1]
  run_iyokan_packet ["convert",
                     "-o", $req_file,
                     "-i", "a", $res_file,
                     "--",
                     "bits.A = a.out",
                     "bits.B = a.out"]
  run_iyokan ["tfhe",
              "--blueprint", "test/config-toml/addr-4bit.toml",
              "--bkey", $bkey,
              "-i", $req_file,
              "-o", $res_file,
              "-c", 1]
  run_iyokan_packet ["dec", "--key", $skey, "--in", $res_file, "--out", $res_file]
  r = run_iyokan_packet ["packet2toml", "--in", $res_file]

  got = TomlRB.parse r
  expected = TomlRB.load_file out_file
  assert_equal_packet got, expected
end

reg.add_in_out "dff-reset-23", "test/config-toml/dff-reset.toml",
               "test/in/test23.in", "test/out/test23.out", ncycles: 1, set_plain_ncycles: true

##### Run
runner = reg.get_runner(tags: ARGV.map(&:to_sym))
runner.run

exit 1 if $has_any_error

#!/usr/bin/ruby

require "shellwords"
require "open3"
require "pathname"
require "json"
require "toml-rb"

$has_any_error = false

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

def run_command(command, args = [])
  path = Pathname.new($path) / command
  cmd = "#{Shellwords.join([path.to_s] + args)}"
  res, err, code = Open3.capture3(cmd)
  raise "Unexpected status code: #{code}\n#{quote(cmd)}\n#{quote(res)}\n#{quote(err)}" if code != 0
  res
end

def run_iyokan(args)
  run_command "./iyokan", args
end

def run_iyokan_packet(args)
  run_command "./iyokan-packet", args
end

def print_error(fh, command, args, ex)
  $has_any_error = true
  fh.puts "\e[31m[ERROR] #{command.inspect} [#{args.map { |s| s.inspect }.join(", ")}]\e[m"
  fh.puts ex.full_message
  fh.puts
end

raise "ruby test.rb PATH [slow]" unless ARGV.size >= 1

$path = ARGV.shift
$path ||= ""

$SLOW_MODE_ENABLED = ARGV.include?("slow")
$CUDA_MODE_ENABLED = ARGV.include?("cuda")

##### test0 #####

run_command "./test0"

##### prepare #####

run_iyokan_packet ["genkey", "--type", "tfhepp", "--out", "_test_sk"]
if $SLOW_MODE_ENABLED
  run_iyokan_packet ["genbkey", "--in", "_test_sk", "--out", "_test_bk"]
end

##### method toml2packet #####

def test_method_toml2packet(in_file, expected)
  toml = TomlRB.load_file(in_file)
  got = toml2packet toml
  assert_equal got, expected
end

test_method_toml2packet "test/test03.in", {
  cycles: -1,
  ram: {},
  rom: {},
  bits: {
    "hoge" => { size: 3, bytes: [5] },
    "piyo" => { size: 3, bytes: [0] },
  },
}

##### iyokan-packet #####

def test_iyokan_packet_e2e(in_file)
  plain_pkt = "_test_plain_packet"
  pkt = "_test_packet"
  skey = "_test_sk"
  bkey = "_test_bk"

  run_iyokan_packet ["toml2packet", "--in", in_file, "--out", plain_pkt]
  if $SLOW_MODE_ENABLED
    run_iyokan_packet ["enc", "--key", skey, "--in", plain_pkt, "--out", pkt]
    run_iyokan_packet ["dec", "--key", skey, "--in", pkt, "--out", plain_pkt]
  end
  r = run_iyokan_packet ["packet2toml", "--in", plain_pkt]

  got = TomlRB.parse(r)
  expected = TomlRB.load_file(in_file)
  assert_equal_packet got, expected
end

test_iyokan_packet_e2e "test/test00.in"
test_iyokan_packet_e2e "test/test00-diamond.out"
test_iyokan_packet_e2e "test/test03.in"

##### iyokan #####

## args1 == nil means tests for TFHEpp won't run. args2 == nil for cuFHE.
## args0 must not be nil. Tests for plain must run.
def test_in_out(blueprint, in_file, out_file, args0 = [], args1 = [], args2 = [])
  raise "args0 must not be nil" if args0.nil?

  plain_req_file = "_test_plain_req_packet"
  plain_res_file = "_test_plain_res_packet"
  req_file = "_test_req_packet"
  res_file = "_test_res_packet"
  secret_key = "_test_sk"
  bkey = "_test_bk"
  snapshot0 = "_test_snapshot0"
  snapshot1 = "_test_snapshot1"
  cycles = -1

  run_iyokan_packet ["toml2packet",
                     "--in", in_file,
                     "--out", plain_req_file]

  ## Check plain mode
  if args0.empty? and not block_given?
    ## Use snapshot
    run_iyokan ["plain",
                "--blueprint", blueprint,
                "-i", plain_req_file,
                "-o", plain_res_file,
                "-c", 1,
                "--snapshot", snapshot0]
    run_iyokan ["plain",
                "--resume", snapshot0,
                "--snapshot", snapshot1]
    run_iyokan ["plain",
                "-c", -1,
                "--resume", snapshot1]
  else
    ## Don't use snapshot in complex situations
    run_iyokan ["plain",
                "--blueprint", blueprint,
                "-i", plain_req_file,
                "-o", plain_res_file] + args0
  end
  r = run_iyokan_packet ["packet2toml", "--in", plain_res_file]
  got = TomlRB.parse(r)
  expected = TomlRB.load_file(out_file)
  assert_equal_packet got, expected
  yield 0 if block_given?
  cycles = got["cycles"]  # Get # of cycles for the rest

  if $SLOW_MODE_ENABLED
    run_iyokan_packet ["enc",
                       "--key", secret_key,
                       "--in", plain_req_file,
                       "--out", req_file]

    unless args1.nil?
      ## Check TFHE mode
      if args1.empty? and not block_given? and cycles > 2
        ## Use snapshot
        run_iyokan ["tfhe",
                    "--blueprint", blueprint,
                    "-c", 1,
                    "--bkey", bkey,
                    "-i", req_file,
                    "-o", res_file,
                    "--snapshot", snapshot0]
        run_iyokan ["tfhe",
                    "--resume", snapshot0,
                    "--bkey", bkey,
                    "--snapshot", snapshot1]
        run_iyokan ["tfhe",
                    "--resume", snapshot1,
                    "--bkey", bkey,
                    "-c", cycles - 2]
      else
        ## Don't use snapshot in complex situations
        run_iyokan ["tfhe",
                    "--blueprint", blueprint,
                    "-c", cycles,
                    "--bkey", bkey,
                    "-i", req_file,
                    "-o", res_file] + args1
      end
      run_iyokan_packet ["dec",
                         "--key", secret_key,
                         "--in", res_file,
                         "--out", plain_res_file]
      r = run_iyokan_packet ["packet2toml", "--in", plain_res_file]
      got = TomlRB.parse(r)
      expected = TomlRB.load_file(out_file)
      assert_equal_packet got, expected
      yield 1 if block_given?
    end

    if $CUDA_MODE_ENABLED
      unless args2.nil?
        ## Check cuFHE mode
        if args2.empty? and not block_given? and cycles > 2
          ## Use snapshot
          run_iyokan ["tfhe",
                      "--enable-gpu",
                      "--blueprint", blueprint,
                      "-c", 1,
                      "--bkey", bkey,
                      "-i", req_file,
                      "-o", res_file,
                      "--snapshot", snapshot0]
          run_iyokan ["tfhe",
                      "--resume", snapshot0,
                      "--bkey", bkey,
                      "--snapshot", snapshot1]
          run_iyokan ["tfhe",
                      "--resume", snapshot1,
                      "--bkey", bkey,
                      "-c", cycles - 2]
        else
          ## Dont use snapshot in complex situations.
          run_iyokan ["tfhe",
                      "--enable-gpu",
                      "--blueprint", blueprint,
                      "-c", cycles,
                      "--bkey", bkey,
                      "-i", req_file,
                      "-o", res_file] + args2
        end
        run_iyokan_packet ["dec",
                           "--key", secret_key,
                           "--in", res_file,
                           "--out", plain_res_file]
        r = run_iyokan_packet ["packet2toml", "--in", plain_res_file]
        got = TomlRB.parse(r)
        expected = TomlRB.load_file(out_file)
        assert_equal_packet got, expected
        yield 2 if block_given?
      end
    end
  end
rescue => ex
  print_error $stderr, "test_in_out", [blueprint, in_file, out_file], ex
end

test_in_out "test/cahp-diamond.toml", "test/test00.in", "test/test00-diamond.out"
test_in_out "test/cahp-emerald.toml", "test/test00.in", "test/test00-emerald.out"
test_in_out "test/cahp-diamond-mux.toml", "test/test00.in", "test/test00-diamond.out"
test_in_out "test/cahp-diamond.toml", "test/test01.in", "test/test01-diamond.out",
            [], nil, [] # Won't do test for TFHEpp
test_in_out "test/cahp-emerald.toml", "test/test01.in", "test/test01-emerald.out",
            [], nil, [] # Won't do test for TFHEpp
test_in_out "test/cahp-diamond-mux.toml", "test/test01.in", "test/test01-diamond.out",
            [], nil, [] # Won't do test for TFHEpp
test_in_out "test/test-addr-4bit.toml", "test/test04.in", "test/test04.out",
            ["-c", 1] # "-c 1" will be automatically set for args1 and args2
test_in_out "test/test-div-8bit.toml", "test/test05.in", "test/test05.out",
            ["-c", 1] # "-c 1" will be automatically set for args1 and args2
test_in_out "test/test-ram-addr8bit.toml", "test/test06.in", "test/test06.out",
            ["-c", 16] # "-c 16" will be automatically set for args1 and args2
test_in_out "test/test-ram-addr9bit.toml", "test/test07.in", "test/test07.out",
            ["-c", 16] # "-c 16" will be automatically set for args1 and args2
test_in_out "test/test-mux-ram-addr8bit.toml", "test/test06.in", "test/test06.out",
            ["-c", 16] # "-c 16" will be automatically set for args1 and args2
test_in_out "test/test-mux-ram-addr9bit.toml", "test/test07.in", "test/test07.out",
            ["-c", 16] # "-c 16" will be automatically set for args1 and args2
test_in_out "test/test-ram-8-16-16.toml", "test/test08.in", "test/test08.out",
            ["-c", 8] # "-c 8" will be automatically set for args1 and args2

test_in_out "test/cahp-diamond.toml", "test/test00.in", "test/test00-diamond.out",
            ["--dump-prefix", "_test_dump"],
            ["--dump-prefix", "_test_dump", "--secret-key", "_test_sk"],
            ["--dump-prefix", "_test_dump", "--secret-key", "_test_sk"] do |_|
  r = run_iyokan_packet ["packet2toml", "--in", "_test_dump-7"]
  toml = TomlRB.parse(r)
  assert_equal toml["cycles"].to_i, 7
  assert_include toml["bits"], { "bytes" => [0], "size" => 1, "name" => "finflag" }
  assert_include toml["bits"], { "bytes" => [42, 0], "size" => 16, "name" => "reg_x0" }
end

exit 1 if $has_any_error

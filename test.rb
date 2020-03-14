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

def check_code_detail(command, args)
  path = Pathname.new($path) / command
  res, _, code = Open3.capture3("#{Shellwords.join([path.to_s] + args)} 2>&1")
  raise "Unexpected status code: #{code}\n#{quote(res)}" if code != 0
  res
end

def print_error(fh, command, args, msg)
  $has_any_error = true
  fh.puts "\e[31m[ERROR] #{command.inspect} [#{args.map { |s| s.inspect }.join(", ")}]\e[m"
  fh.puts msg
  fh.puts
end

def check_code(command, args = [])
  check_code_detail(command, args)
rescue => ex
  print_error $stderr, "check_code", [command] + args, ex.message
end

def test_iyokan(args)
  res = check_code_detail "./iyokan", args
  yield res
rescue => ex
  print_error $stderr, "test_iyokan", args, ex.message
end

raise "ruby test.rb PATH [slow]" unless ARGV.size >= 1

$path = ARGV.shift
$path ||= ""

$SLOW_MODE_ENABLED = ARGV.include?("slow")
$CUDA_MODE_ENABLED = ARGV.include?("cuda")

##### test0 #####

check_code "./test0"

##### iyokan #####

check_code "./iyokan-packet", ["toml2packet",
                               "--in", "test/test00-packet.toml",
                               "--out", "_test_plain_req_packet00"]
check_code "./iyokan-packet", ["toml2packet",
                               "--in", "test/test01-packet.toml",
                               "--out", "_test_plain_req_packet01"]

test_iyokan [
  "plain",
  "--blueprint", "test/cahp-diamond.toml",
  "-i", "_test_plain_req_packet00",
  "-o", "_test_plain_res_packet00",
] do |_|
  r = check_code "./iyokan-packet", ["unpack", "--in", "_test_plain_res_packet00"]
  toml = TomlRB.parse(r)
  assert_equal toml["cycles"].to_i, 8
  assert_include toml["bits"], { "bytes" => [1], "size" => 1, "name" => "finflag" }
  assert_include toml["bits"], { "bytes" => [42, 0], "size" => 16, "name" => "reg_x0" }
end

test_iyokan [
  "plain",
  "--blueprint", "test/cahp-diamond.toml",
  "-i", "_test_plain_req_packet01",
  "-o", "_test_plain_res_packet01",
] do |_|
  r = check_code "./iyokan-packet", ["unpack", "--in", "_test_plain_res_packet01"]
  toml = TomlRB.parse(r)
  assert_equal toml["cycles"].to_i, 346
  assert_include toml["bits"], { "bytes" => [1], "size" => 1, "name" => "finflag" }
  assert_include toml["bits"], { "bytes" => [5, 0], "size" => 16, "name" => "reg_x8" }

  assert_include toml["ram"], { "bytes" => [0] * 256, "size" => 2048, "name" => "ramA" }
  assert_include toml["ram"], {
                   "bytes" => [0] * 239 +
                              [1, 2, 39, 1, 2, 39, 1, 3, 39, 3, 5, 39, 0, 0, 59, 5, 0],
                   "size" => 2048,
                   "name" => "ramB",
                 }
end

test_iyokan [
  "plain",
  "--blueprint", "test/cahp-emerald.toml",
  "-i", "_test_plain_req_packet01",
  "-o", "_test_plain_res_packet01",
] do |_|
  r = check_code "./iyokan-packet", ["unpack", "--in", "_test_plain_res_packet01"]
  toml = TomlRB.parse(r)
  assert_equal toml["cycles"].to_i, 261
  assert_include toml["bits"], { "bytes" => [1], "size" => 1, "name" => "finflag" }
  assert_include toml["bits"], { "bytes" => [5, 0], "size" => 16, "name" => "reg_x8" }

  assert_include toml["ram"], { "bytes" => [0] * 256, "size" => 2048, "name" => "ramA" }
  assert_include toml["ram"], {
                   "bytes" => [0] * 239 +
                              [1, 2, 39, 1, 2, 39, 1, 3, 39, 3, 5, 39, 0, 0, 59, 5, 0],
                   "size" => 2048,
                   "name" => "ramB",
                 }
end

test_iyokan [
  "plain",
  "--blueprint", "test/cahp-diamond.toml",
  "-i", "_test_plain_req_packet00",
  "-o", "_test_plain_res_packet00",
  "--dump-prefix", "_test_dump",
] do |r|
  r = check_code "./iyokan-packet", ["unpack", "--in", "_test_dump-7"]
  toml = TomlRB.parse(r)
  assert_equal toml["cycles"].to_i, 7
  assert_include toml["bits"], { "bytes" => [0], "size" => 1, "name" => "finflag" }
  assert_include toml["bits"], { "bytes" => [42, 0], "size" => 16, "name" => "reg_x0" }
end

if $SLOW_MODE_ENABLED
  check_code "./iyokan-packet", ["genkey", "--type", "tfhepp", "--out", "_test_sk"]
  check_code "./iyokan-packet", ["enc",
                                 "--key", "_test_sk",
                                 "--in", "_test_plain_req_packet00",
                                 "--out", "_test_req_packet00"]
  check_code "./iyokan-packet", ["enc",
                                 "--key", "_test_sk",
                                 "--in", "_test_plain_req_packet01",
                                 "--out", "_test_req_packet01"]

  test_iyokan [
    "tfhe",
    "--blueprint", "test/cahp-diamond.toml",
    "-i", "_test_req_packet00",
    "-o", "_test_res_packet00",
    "-c", "8",
  ] do |_|
    r = check_code "./iyokan-packet", ["unpack", "--in", "_test_res_packet00"]
    toml = TomlRB.parse(r)
    assert_equal toml["cycles"].to_i, 8
    assert_include toml["bits"], { "bytes" => [1], "size" => 1, "name" => "finflag" }
    assert_include toml["bits"], { "bytes" => [42, 0], "size" => 16, "name" => "reg_x0" }
  end

  test_iyokan [
    "tfhe",
    "--blueprint", "test/cahp-diamond.toml",
    "-i", "_test_req_packet00",
    "-o", "_test_res_packet00",
    "-c", "8",
    "--dump-prefix", "_test_dump",
    "--secret-key", "_test_sk",
  ] do |r|
    r = check_code "./iyokan-packet", ["unpack", "--in", "_test_dump-7"]
    toml = TomlRB.parse(r)
    assert_equal toml["cycles"].to_i, 7
    assert_include toml["bits"], { "bytes" => [0], "size" => 1, "name" => "finflag" }
    assert_include toml["bits"], { "bytes" => [42, 0], "size" => 16, "name" => "reg_x0" }
  end

  if $CUDA_MODE_ENABLED
    test_iyokan [
      "tfhe",
      "--blueprint", "test/cahp-diamond.toml",
      "-i", "_test_req_packet00",
      "-o", "_test_res_packet00",
      "-c", "8",
      "--enable-gpu",
    ] do |_|
      r = check_code "./iyokan-packet", ["unpack", "--in", "_test_res_packet00"]
      toml = TomlRB.parse(r)
      assert_equal toml["cycles"].to_i, 8
      assert_include toml["bits"], { "bytes" => [1], "size" => 1, "name" => "finflag" }
      assert_include toml["bits"], { "bytes" => [42, 0], "size" => 16, "name" => "reg_x0" }
    end

    test_iyokan [
      "tfhe",
      "--blueprint", "test/cahp-diamond.toml",
      "-i", "_test_req_packet00",
      "-o", "_test_res_packet00",
      "-c", "8",
      "--enable-gpu",
      "--dump-prefix", "_test_dump",
      "--secret-key", "_test_sk",
    ] do |r|
      r = check_code "./iyokan-packet", ["unpack", "--in", "_test_dump-7"]
      toml = TomlRB.parse(r)
      assert_equal toml["cycles"].to_i, 7
      assert_include toml["bits"], { "bytes" => [0], "size" => 1, "name" => "finflag" }
      assert_include toml["bits"], { "bytes" => [42, 0], "size" => 16, "name" => "reg_x0" }
    end

    test_iyokan [
      "tfhe",
      "--blueprint", "test/cahp-diamond.toml",
      "-i", "_test_req_packet01",
      "-o", "_test_res_packet01",
      "-c", "346",
      "--enable-gpu",
    ] do |_|
      r = check_code "./iyokan-packet", ["unpack", "--in", "_test_res_packet01"]
      toml = TomlRB.parse(r)
      assert_equal toml["cycles"].to_i, 346
      assert_include toml["bits"], { "bytes" => [1], "size" => 1, "name" => "finflag" }
      assert_include toml["bits"], { "bytes" => [5, 0], "size" => 16, "name" => "reg_x8" }

      assert_include toml["ram"], { "bytes" => [0] * 256, "size" => 2048, "name" => "ramA" }
      assert_include toml["ram"], {
                       "bytes" => [0] * 239 +
                                  [1, 2, 39, 1, 2, 39, 1, 3, 39, 3, 5, 39, 0, 0, 59, 5, 0],
                       "size" => 2048,
                       "name" => "ramB",
                     }
    end

    test_iyokan [
      "tfhe",
      "--blueprint", "test/cahp-emerald.toml",
      "-i", "_test_req_packet01",
      "-o", "_test_res_packet01",
      "-c", "261",
      "--enable-gpu",
    ] do |_|
      r = check_code "./iyokan-packet", ["unpack", "--in", "_test_res_packet01"]
      toml = TomlRB.parse(r)
      assert_equal toml["cycles"].to_i, 261
      assert_include toml["bits"], { "bytes" => [1], "size" => 1, "name" => "finflag" }
      assert_include toml["bits"], { "bytes" => [5, 0], "size" => 16, "name" => "reg_x8" }

      assert_include toml["ram"], { "bytes" => [0] * 256, "size" => 2048, "name" => "ramA" }
      assert_include toml["ram"], {
                       "bytes" => [0] * 239 +
                                  [1, 2, 39, 1, 2, 39, 1, 3, 39, 3, 5, 39, 0, 0, 59, 5, 0],
                       "size" => 2048,
                       "name" => "ramB",
                     }
    end
  end
end

exit 1 if $has_any_error

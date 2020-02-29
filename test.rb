#!/usr/bin/ruby

require "shellwords"
require "open3"
require "pathname"
require "json"

$has_any_error = false

##### utility #####

def quote(str, prefix = "> ")
  prefix + str.gsub("\n", "\n#{prefix}")
end

##### assert #####

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
check_code "./test0", ["slow"] if $SLOW_MODE_ENABLED

##### iyokan #####

check_code "./kvsp-packet", ["plain", "test/test00.elf", "_test_plain_req_packet00"]

test_iyokan [
  "plain",
  "-l", "test/diamond-core-wo-ram-rom.json",
  "-i", "_test_plain_req_packet00",
  "--enable-rom", "io_romAddr:7:io_romData:32",
  "--enable-ram",
] do |r|
  assert_regex r, /#cycle\t8/
  assert_regex r, /f0\t1/
  assert_regex r, /x0\t42/
end

test_iyokan [
  "plain",
  "-l", "test/diamond-core-wo-ram-rom.json",
  "-i", "_test_plain_req_packet00",
  "--enable-rom", "io_romAddr:7:io_romData:32",
  "--enable-ram",
  "--enable-dump-every-clock", "_test_dump",
] do |r|
  json = JSON.parse(open("_test_dump").read.lines[-1])
  assert_include json, { "type" => "flag", "addr" => 0, "byte" => true }
  assert_include json, { "type" => "reg", "addr" => 0, "byte" => 42 }
end

if $SLOW_MODE_ENABLED
  check_code "./kvsp-packet", ["genkey", "_test_sk"]
  check_code "./kvsp-packet", ["enc", "_test_sk", "test/test00.elf", "_test_req_packet00"]

  test_iyokan [
    "tfhe",
    "-l", "test/diamond-core-wo-ram-rom.json",
    "-i", "_test_req_packet00",
    "-o", "_test_res_packet00",
    "-c", "8",
    "--enable-rom", "io_romAddr:7:io_romData:32",
    "--enable-ram",
  ] do |_|
    r = check_code "./kvsp-packet", ["dec", "_test_sk", "_test_res_packet00"]
    assert_regex r, /#cycle\t8/
    assert_regex r, /f0\t1/
    assert_regex r, /x0\t42/
  end

  if $CUDA_MODE_ENABLED
    test_iyokan [
      "tfhe",
      "-l", "test/diamond-core-wo-ram-rom.json",
      "-i", "_test_req_packet00",
      "-o", "_test_res_packet00",
      "-c", "8",
      "--enable-rom", "io_romAddr:7:io_romData:32",
      "--enable-ram",
      "--enable-gpu",
    ] do |_|
      r = check_code "./kvsp-packet", ["dec", "_test_sk", "_test_res_packet00"]
      assert_regex r, /#cycle\t8/
      assert_regex r, /f0\t1/
      assert_regex r, /x0\t42/
    end
  end
end

exit 1 if $has_any_error

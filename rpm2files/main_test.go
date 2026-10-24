package main

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"io"
	"os"
	"path/filepath"
	"testing"

	"github.com/klauspost/compress/zstd"
)

const (
	memberName    = "etc/hello"
	memberContent = "hello zstd\n"
)

func plainTar(t *testing.T) []byte {
	t.Helper()
	var buf bytes.Buffer
	tw := tar.NewWriter(&buf)
	if err := tw.WriteHeader(&tar.Header{Name: memberName, Mode: 0o644, Size: int64(len(memberContent)), Typeflag: tar.TypeReg}); err != nil {
		t.Fatal(err)
	}
	if _, err := tw.Write([]byte(memberContent)); err != nil {
		t.Fatal(err)
	}
	if err := tw.Close(); err != nil {
		t.Fatal(err)
	}
	return buf.Bytes()
}

func gzipBytes(t *testing.T, in []byte) []byte {
	t.Helper()
	var buf bytes.Buffer
	zw := gzip.NewWriter(&buf)
	if _, err := zw.Write(in); err != nil {
		t.Fatal(err)
	}
	if err := zw.Close(); err != nil {
		t.Fatal(err)
	}
	return buf.Bytes()
}

func zstdBytes(t *testing.T, in []byte) []byte {
	t.Helper()
	var buf bytes.Buffer
	zw, err := zstd.NewWriter(&buf)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := zw.Write(in); err != nil {
		t.Fatal(err)
	}
	if err := zw.Close(); err != nil {
		t.Fatal(err)
	}
	return buf.Bytes()
}

func TestOpenArchive(t *testing.T) {
	t.Parallel()
	tarData := plainTar(t)
	tests := []struct {
		name string
		data []byte
	}{
		{"plain", tarData},
		{"gzip", gzipBytes(t, tarData)},
		{"zstd", zstdBytes(t, tarData)},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			t.Parallel()
			p := filepath.Join(t.TempDir(), "archive")
			if err := os.WriteFile(p, tc.data, 0o644); err != nil {
				t.Fatal(err)
			}
			rc, err := openArchive(p)
			if err != nil {
				t.Fatalf("openArchive: %v", err)
			}
			defer func() { _ = rc.Close() }()
			tr := tar.NewReader(rc)
			hdr, err := tr.Next()
			if err != nil {
				t.Fatalf("tar next: %v", err)
			}
			if hdr.Name != memberName {
				t.Errorf("member name = %q, want %q", hdr.Name, memberName)
			}
			got, err := io.ReadAll(tr)
			if err != nil {
				t.Fatal(err)
			}
			if string(got) != memberContent {
				t.Errorf("content = %q, want %q", got, memberContent)
			}
		})
	}
}

func TestOpenArchiveMissing(t *testing.T) {
	t.Parallel()
	if _, err := openArchive(filepath.Join(t.TempDir(), "nope")); err == nil {
		t.Fatal("expected error for missing archive")
	}
}

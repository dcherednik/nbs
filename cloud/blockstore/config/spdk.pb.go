// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.26.0
// 	protoc        v3.19.0
// source: cloud/blockstore/config/spdk.proto

package config

import (
	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	reflect "reflect"
	sync "sync"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

type TSpdkEnvConfig struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	// CPU core mask.
	CpuMask *string `protobuf:"bytes,1,opt,name=CpuMask" json:"CpuMask,omitempty"`
	// Hugetlbfs mount point
	HugeDir *string `protobuf:"bytes,2,opt,name=HugeDir" json:"HugeDir,omitempty"`
}

func (x *TSpdkEnvConfig) Reset() {
	*x = TSpdkEnvConfig{}
	if protoimpl.UnsafeEnabled {
		mi := &file_cloud_blockstore_config_spdk_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TSpdkEnvConfig) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TSpdkEnvConfig) ProtoMessage() {}

func (x *TSpdkEnvConfig) ProtoReflect() protoreflect.Message {
	mi := &file_cloud_blockstore_config_spdk_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TSpdkEnvConfig.ProtoReflect.Descriptor instead.
func (*TSpdkEnvConfig) Descriptor() ([]byte, []int) {
	return file_cloud_blockstore_config_spdk_proto_rawDescGZIP(), []int{0}
}

func (x *TSpdkEnvConfig) GetCpuMask() string {
	if x != nil && x.CpuMask != nil {
		return *x.CpuMask
	}
	return ""
}

func (x *TSpdkEnvConfig) GetHugeDir() string {
	if x != nil && x.HugeDir != nil {
		return *x.HugeDir
	}
	return ""
}

var File_cloud_blockstore_config_spdk_proto protoreflect.FileDescriptor

var file_cloud_blockstore_config_spdk_proto_rawDesc = []byte{
	0x0a, 0x22, 0x63, 0x6c, 0x6f, 0x75, 0x64, 0x2f, 0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x73, 0x74, 0x6f,
	0x72, 0x65, 0x2f, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x2f, 0x73, 0x70, 0x64, 0x6b, 0x2e, 0x70,
	0x72, 0x6f, 0x74, 0x6f, 0x12, 0x19, 0x4e, 0x43, 0x6c, 0x6f, 0x75, 0x64, 0x2e, 0x4e, 0x42, 0x6c,
	0x6f, 0x63, 0x6b, 0x53, 0x74, 0x6f, 0x72, 0x65, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x22,
	0x44, 0x0a, 0x0e, 0x54, 0x53, 0x70, 0x64, 0x6b, 0x45, 0x6e, 0x76, 0x43, 0x6f, 0x6e, 0x66, 0x69,
	0x67, 0x12, 0x18, 0x0a, 0x07, 0x43, 0x70, 0x75, 0x4d, 0x61, 0x73, 0x6b, 0x18, 0x01, 0x20, 0x01,
	0x28, 0x09, 0x52, 0x07, 0x43, 0x70, 0x75, 0x4d, 0x61, 0x73, 0x6b, 0x12, 0x18, 0x0a, 0x07, 0x48,
	0x75, 0x67, 0x65, 0x44, 0x69, 0x72, 0x18, 0x02, 0x20, 0x01, 0x28, 0x09, 0x52, 0x07, 0x48, 0x75,
	0x67, 0x65, 0x44, 0x69, 0x72, 0x42, 0x2a, 0x5a, 0x28, 0x61, 0x2e, 0x79, 0x61, 0x6e, 0x64, 0x65,
	0x78, 0x2d, 0x74, 0x65, 0x61, 0x6d, 0x2e, 0x72, 0x75, 0x2f, 0x63, 0x6c, 0x6f, 0x75, 0x64, 0x2f,
	0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x2f, 0x63, 0x6f, 0x6e, 0x66, 0x69,
	0x67,
}

var (
	file_cloud_blockstore_config_spdk_proto_rawDescOnce sync.Once
	file_cloud_blockstore_config_spdk_proto_rawDescData = file_cloud_blockstore_config_spdk_proto_rawDesc
)

func file_cloud_blockstore_config_spdk_proto_rawDescGZIP() []byte {
	file_cloud_blockstore_config_spdk_proto_rawDescOnce.Do(func() {
		file_cloud_blockstore_config_spdk_proto_rawDescData = protoimpl.X.CompressGZIP(file_cloud_blockstore_config_spdk_proto_rawDescData)
	})
	return file_cloud_blockstore_config_spdk_proto_rawDescData
}

var file_cloud_blockstore_config_spdk_proto_msgTypes = make([]protoimpl.MessageInfo, 1)
var file_cloud_blockstore_config_spdk_proto_goTypes = []interface{}{
	(*TSpdkEnvConfig)(nil), // 0: NCloud.NBlockStore.NProto.TSpdkEnvConfig
}
var file_cloud_blockstore_config_spdk_proto_depIdxs = []int32{
	0, // [0:0] is the sub-list for method output_type
	0, // [0:0] is the sub-list for method input_type
	0, // [0:0] is the sub-list for extension type_name
	0, // [0:0] is the sub-list for extension extendee
	0, // [0:0] is the sub-list for field type_name
}

func init() { file_cloud_blockstore_config_spdk_proto_init() }
func file_cloud_blockstore_config_spdk_proto_init() {
	if File_cloud_blockstore_config_spdk_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_cloud_blockstore_config_spdk_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TSpdkEnvConfig); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
	}
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_cloud_blockstore_config_spdk_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   1,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_cloud_blockstore_config_spdk_proto_goTypes,
		DependencyIndexes: file_cloud_blockstore_config_spdk_proto_depIdxs,
		MessageInfos:      file_cloud_blockstore_config_spdk_proto_msgTypes,
	}.Build()
	File_cloud_blockstore_config_spdk_proto = out.File
	file_cloud_blockstore_config_spdk_proto_rawDesc = nil
	file_cloud_blockstore_config_spdk_proto_goTypes = nil
	file_cloud_blockstore_config_spdk_proto_depIdxs = nil
}

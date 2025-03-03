// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.26.0
// 	protoc        v3.19.0
// source: cloud/storage/core/config/iam.proto

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

type TIamClientConfig struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	// Metadata service url.
	MetadataServiceUrl *string `protobuf:"bytes,1,opt,name=MetadataServiceUrl" json:"MetadataServiceUrl,omitempty"`
	// Token Agent socket path.
	TokenAgentUnixSocket *string `protobuf:"bytes,2,opt,name=TokenAgentUnixSocket" json:"TokenAgentUnixSocket,omitempty"`
	// Token request timeout in durable client.
	InitialRetryTimeout *uint32 `protobuf:"varint,3,opt,name=InitialRetryTimeout" json:"InitialRetryTimeout,omitempty"` // in milliseconds
	// Backoff timeout increment in durable client.
	RetryTimeoutIncrement *uint32 `protobuf:"varint,4,opt,name=RetryTimeoutIncrement" json:"RetryTimeoutIncrement,omitempty"` // in milliseconds
	// Number of attempts in durable client.
	RetryAttempts *uint32 `protobuf:"varint,5,opt,name=RetryAttempts" json:"RetryAttempts,omitempty"`
	// GRPC request timeout.
	GrpcTimeout *uint32 `protobuf:"varint,6,opt,name=GrpcTimeout" json:"GrpcTimeout,omitempty"` // in milliseconds
	// Token refresh timeout.
	TokenRefreshTimeout *uint32 `protobuf:"varint,7,opt,name=TokenRefreshTimeout" json:"TokenRefreshTimeout,omitempty"` // in milliseconds
	// HTTP request timeout.
	HttpTimeout *uint32 `protobuf:"varint,8,opt,name=HttpTimeout" json:"HttpTimeout,omitempty"` // in milliseconds
}

func (x *TIamClientConfig) Reset() {
	*x = TIamClientConfig{}
	if protoimpl.UnsafeEnabled {
		mi := &file_cloud_storage_core_config_iam_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TIamClientConfig) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TIamClientConfig) ProtoMessage() {}

func (x *TIamClientConfig) ProtoReflect() protoreflect.Message {
	mi := &file_cloud_storage_core_config_iam_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TIamClientConfig.ProtoReflect.Descriptor instead.
func (*TIamClientConfig) Descriptor() ([]byte, []int) {
	return file_cloud_storage_core_config_iam_proto_rawDescGZIP(), []int{0}
}

func (x *TIamClientConfig) GetMetadataServiceUrl() string {
	if x != nil && x.MetadataServiceUrl != nil {
		return *x.MetadataServiceUrl
	}
	return ""
}

func (x *TIamClientConfig) GetTokenAgentUnixSocket() string {
	if x != nil && x.TokenAgentUnixSocket != nil {
		return *x.TokenAgentUnixSocket
	}
	return ""
}

func (x *TIamClientConfig) GetInitialRetryTimeout() uint32 {
	if x != nil && x.InitialRetryTimeout != nil {
		return *x.InitialRetryTimeout
	}
	return 0
}

func (x *TIamClientConfig) GetRetryTimeoutIncrement() uint32 {
	if x != nil && x.RetryTimeoutIncrement != nil {
		return *x.RetryTimeoutIncrement
	}
	return 0
}

func (x *TIamClientConfig) GetRetryAttempts() uint32 {
	if x != nil && x.RetryAttempts != nil {
		return *x.RetryAttempts
	}
	return 0
}

func (x *TIamClientConfig) GetGrpcTimeout() uint32 {
	if x != nil && x.GrpcTimeout != nil {
		return *x.GrpcTimeout
	}
	return 0
}

func (x *TIamClientConfig) GetTokenRefreshTimeout() uint32 {
	if x != nil && x.TokenRefreshTimeout != nil {
		return *x.TokenRefreshTimeout
	}
	return 0
}

func (x *TIamClientConfig) GetHttpTimeout() uint32 {
	if x != nil && x.HttpTimeout != nil {
		return *x.HttpTimeout
	}
	return 0
}

var File_cloud_storage_core_config_iam_proto protoreflect.FileDescriptor

var file_cloud_storage_core_config_iam_proto_rawDesc = []byte{
	0x0a, 0x23, 0x63, 0x6c, 0x6f, 0x75, 0x64, 0x2f, 0x73, 0x74, 0x6f, 0x72, 0x61, 0x67, 0x65, 0x2f,
	0x63, 0x6f, 0x72, 0x65, 0x2f, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x2f, 0x69, 0x61, 0x6d, 0x2e,
	0x70, 0x72, 0x6f, 0x74, 0x6f, 0x12, 0x0d, 0x4e, 0x43, 0x6c, 0x6f, 0x75, 0x64, 0x2e, 0x4e, 0x50,
	0x72, 0x6f, 0x74, 0x6f, 0x22, 0xfa, 0x02, 0x0a, 0x10, 0x54, 0x49, 0x61, 0x6d, 0x43, 0x6c, 0x69,
	0x65, 0x6e, 0x74, 0x43, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x12, 0x2e, 0x0a, 0x12, 0x4d, 0x65, 0x74,
	0x61, 0x64, 0x61, 0x74, 0x61, 0x53, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x55, 0x72, 0x6c, 0x18,
	0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x12, 0x4d, 0x65, 0x74, 0x61, 0x64, 0x61, 0x74, 0x61, 0x53,
	0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x55, 0x72, 0x6c, 0x12, 0x32, 0x0a, 0x14, 0x54, 0x6f, 0x6b,
	0x65, 0x6e, 0x41, 0x67, 0x65, 0x6e, 0x74, 0x55, 0x6e, 0x69, 0x78, 0x53, 0x6f, 0x63, 0x6b, 0x65,
	0x74, 0x18, 0x02, 0x20, 0x01, 0x28, 0x09, 0x52, 0x14, 0x54, 0x6f, 0x6b, 0x65, 0x6e, 0x41, 0x67,
	0x65, 0x6e, 0x74, 0x55, 0x6e, 0x69, 0x78, 0x53, 0x6f, 0x63, 0x6b, 0x65, 0x74, 0x12, 0x30, 0x0a,
	0x13, 0x49, 0x6e, 0x69, 0x74, 0x69, 0x61, 0x6c, 0x52, 0x65, 0x74, 0x72, 0x79, 0x54, 0x69, 0x6d,
	0x65, 0x6f, 0x75, 0x74, 0x18, 0x03, 0x20, 0x01, 0x28, 0x0d, 0x52, 0x13, 0x49, 0x6e, 0x69, 0x74,
	0x69, 0x61, 0x6c, 0x52, 0x65, 0x74, 0x72, 0x79, 0x54, 0x69, 0x6d, 0x65, 0x6f, 0x75, 0x74, 0x12,
	0x34, 0x0a, 0x15, 0x52, 0x65, 0x74, 0x72, 0x79, 0x54, 0x69, 0x6d, 0x65, 0x6f, 0x75, 0x74, 0x49,
	0x6e, 0x63, 0x72, 0x65, 0x6d, 0x65, 0x6e, 0x74, 0x18, 0x04, 0x20, 0x01, 0x28, 0x0d, 0x52, 0x15,
	0x52, 0x65, 0x74, 0x72, 0x79, 0x54, 0x69, 0x6d, 0x65, 0x6f, 0x75, 0x74, 0x49, 0x6e, 0x63, 0x72,
	0x65, 0x6d, 0x65, 0x6e, 0x74, 0x12, 0x24, 0x0a, 0x0d, 0x52, 0x65, 0x74, 0x72, 0x79, 0x41, 0x74,
	0x74, 0x65, 0x6d, 0x70, 0x74, 0x73, 0x18, 0x05, 0x20, 0x01, 0x28, 0x0d, 0x52, 0x0d, 0x52, 0x65,
	0x74, 0x72, 0x79, 0x41, 0x74, 0x74, 0x65, 0x6d, 0x70, 0x74, 0x73, 0x12, 0x20, 0x0a, 0x0b, 0x47,
	0x72, 0x70, 0x63, 0x54, 0x69, 0x6d, 0x65, 0x6f, 0x75, 0x74, 0x18, 0x06, 0x20, 0x01, 0x28, 0x0d,
	0x52, 0x0b, 0x47, 0x72, 0x70, 0x63, 0x54, 0x69, 0x6d, 0x65, 0x6f, 0x75, 0x74, 0x12, 0x30, 0x0a,
	0x13, 0x54, 0x6f, 0x6b, 0x65, 0x6e, 0x52, 0x65, 0x66, 0x72, 0x65, 0x73, 0x68, 0x54, 0x69, 0x6d,
	0x65, 0x6f, 0x75, 0x74, 0x18, 0x07, 0x20, 0x01, 0x28, 0x0d, 0x52, 0x13, 0x54, 0x6f, 0x6b, 0x65,
	0x6e, 0x52, 0x65, 0x66, 0x72, 0x65, 0x73, 0x68, 0x54, 0x69, 0x6d, 0x65, 0x6f, 0x75, 0x74, 0x12,
	0x20, 0x0a, 0x0b, 0x48, 0x74, 0x74, 0x70, 0x54, 0x69, 0x6d, 0x65, 0x6f, 0x75, 0x74, 0x18, 0x08,
	0x20, 0x01, 0x28, 0x0d, 0x52, 0x0b, 0x48, 0x74, 0x74, 0x70, 0x54, 0x69, 0x6d, 0x65, 0x6f, 0x75,
	0x74, 0x42, 0x2c, 0x5a, 0x2a, 0x61, 0x2e, 0x79, 0x61, 0x6e, 0x64, 0x65, 0x78, 0x2d, 0x74, 0x65,
	0x61, 0x6d, 0x2e, 0x72, 0x75, 0x2f, 0x63, 0x6c, 0x6f, 0x75, 0x64, 0x2f, 0x73, 0x74, 0x6f, 0x72,
	0x61, 0x67, 0x65, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67,
}

var (
	file_cloud_storage_core_config_iam_proto_rawDescOnce sync.Once
	file_cloud_storage_core_config_iam_proto_rawDescData = file_cloud_storage_core_config_iam_proto_rawDesc
)

func file_cloud_storage_core_config_iam_proto_rawDescGZIP() []byte {
	file_cloud_storage_core_config_iam_proto_rawDescOnce.Do(func() {
		file_cloud_storage_core_config_iam_proto_rawDescData = protoimpl.X.CompressGZIP(file_cloud_storage_core_config_iam_proto_rawDescData)
	})
	return file_cloud_storage_core_config_iam_proto_rawDescData
}

var file_cloud_storage_core_config_iam_proto_msgTypes = make([]protoimpl.MessageInfo, 1)
var file_cloud_storage_core_config_iam_proto_goTypes = []interface{}{
	(*TIamClientConfig)(nil), // 0: NCloud.NProto.TIamClientConfig
}
var file_cloud_storage_core_config_iam_proto_depIdxs = []int32{
	0, // [0:0] is the sub-list for method output_type
	0, // [0:0] is the sub-list for method input_type
	0, // [0:0] is the sub-list for extension type_name
	0, // [0:0] is the sub-list for extension extendee
	0, // [0:0] is the sub-list for field type_name
}

func init() { file_cloud_storage_core_config_iam_proto_init() }
func file_cloud_storage_core_config_iam_proto_init() {
	if File_cloud_storage_core_config_iam_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_cloud_storage_core_config_iam_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TIamClientConfig); i {
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
			RawDescriptor: file_cloud_storage_core_config_iam_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   1,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_cloud_storage_core_config_iam_proto_goTypes,
		DependencyIndexes: file_cloud_storage_core_config_iam_proto_depIdxs,
		MessageInfos:      file_cloud_storage_core_config_iam_proto_msgTypes,
	}.Build()
	File_cloud_storage_core_config_iam_proto = out.File
	file_cloud_storage_core_config_iam_proto_rawDesc = nil
	file_cloud_storage_core_config_iam_proto_goTypes = nil
	file_cloud_storage_core_config_iam_proto_depIdxs = nil
}

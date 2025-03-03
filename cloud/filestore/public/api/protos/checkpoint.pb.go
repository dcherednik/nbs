// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.26.0
// 	protoc        v3.19.0
// source: cloud/filestore/public/api/protos/checkpoint.proto

package protos

import (
	protos "github.com/ydb-platform/nbs/cloud/storage/core/protos"
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

type TCreateCheckpointRequest struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	// Optional request headers.
	Headers *THeaders `protobuf:"bytes,1,opt,name=Headers,proto3" json:"Headers,omitempty"`
	// FileSystem identifier.
	FileSystemId string `protobuf:"bytes,2,opt,name=FileSystemId,proto3" json:"FileSystemId,omitempty"`
	// Checkpoint identifier.
	CheckpointId string `protobuf:"bytes,3,opt,name=CheckpointId,proto3" json:"CheckpointId,omitempty"`
	// Root node (covered by checkpoint)
	NodeId uint64 `protobuf:"varint,4,opt,name=NodeId,proto3" json:"NodeId,omitempty"`
}

func (x *TCreateCheckpointRequest) Reset() {
	*x = TCreateCheckpointRequest{}
	if protoimpl.UnsafeEnabled {
		mi := &file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TCreateCheckpointRequest) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TCreateCheckpointRequest) ProtoMessage() {}

func (x *TCreateCheckpointRequest) ProtoReflect() protoreflect.Message {
	mi := &file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TCreateCheckpointRequest.ProtoReflect.Descriptor instead.
func (*TCreateCheckpointRequest) Descriptor() ([]byte, []int) {
	return file_cloud_filestore_public_api_protos_checkpoint_proto_rawDescGZIP(), []int{0}
}

func (x *TCreateCheckpointRequest) GetHeaders() *THeaders {
	if x != nil {
		return x.Headers
	}
	return nil
}

func (x *TCreateCheckpointRequest) GetFileSystemId() string {
	if x != nil {
		return x.FileSystemId
	}
	return ""
}

func (x *TCreateCheckpointRequest) GetCheckpointId() string {
	if x != nil {
		return x.CheckpointId
	}
	return ""
}

func (x *TCreateCheckpointRequest) GetNodeId() uint64 {
	if x != nil {
		return x.NodeId
	}
	return 0
}

type TCreateCheckpointResponse struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	// Optional error, set only if error happened.
	Error *protos.TError `protobuf:"bytes,1,opt,name=Error,proto3" json:"Error,omitempty"`
	// Optional response headers.
	Headers *TResponseHeaders `protobuf:"bytes,1000,opt,name=Headers,proto3" json:"Headers,omitempty"`
}

func (x *TCreateCheckpointResponse) Reset() {
	*x = TCreateCheckpointResponse{}
	if protoimpl.UnsafeEnabled {
		mi := &file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TCreateCheckpointResponse) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TCreateCheckpointResponse) ProtoMessage() {}

func (x *TCreateCheckpointResponse) ProtoReflect() protoreflect.Message {
	mi := &file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TCreateCheckpointResponse.ProtoReflect.Descriptor instead.
func (*TCreateCheckpointResponse) Descriptor() ([]byte, []int) {
	return file_cloud_filestore_public_api_protos_checkpoint_proto_rawDescGZIP(), []int{1}
}

func (x *TCreateCheckpointResponse) GetError() *protos.TError {
	if x != nil {
		return x.Error
	}
	return nil
}

func (x *TCreateCheckpointResponse) GetHeaders() *TResponseHeaders {
	if x != nil {
		return x.Headers
	}
	return nil
}

type TDestroyCheckpointRequest struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	// Optional request headers.
	Headers *THeaders `protobuf:"bytes,1,opt,name=Headers,proto3" json:"Headers,omitempty"`
	// FileSystem identifier.
	FileSystemId string `protobuf:"bytes,2,opt,name=FileSystemId,proto3" json:"FileSystemId,omitempty"`
	// Checkpoint identifier.
	CheckpointId string `protobuf:"bytes,3,opt,name=CheckpointId,proto3" json:"CheckpointId,omitempty"`
}

func (x *TDestroyCheckpointRequest) Reset() {
	*x = TDestroyCheckpointRequest{}
	if protoimpl.UnsafeEnabled {
		mi := &file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes[2]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TDestroyCheckpointRequest) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TDestroyCheckpointRequest) ProtoMessage() {}

func (x *TDestroyCheckpointRequest) ProtoReflect() protoreflect.Message {
	mi := &file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes[2]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TDestroyCheckpointRequest.ProtoReflect.Descriptor instead.
func (*TDestroyCheckpointRequest) Descriptor() ([]byte, []int) {
	return file_cloud_filestore_public_api_protos_checkpoint_proto_rawDescGZIP(), []int{2}
}

func (x *TDestroyCheckpointRequest) GetHeaders() *THeaders {
	if x != nil {
		return x.Headers
	}
	return nil
}

func (x *TDestroyCheckpointRequest) GetFileSystemId() string {
	if x != nil {
		return x.FileSystemId
	}
	return ""
}

func (x *TDestroyCheckpointRequest) GetCheckpointId() string {
	if x != nil {
		return x.CheckpointId
	}
	return ""
}

type TDestroyCheckpointResponse struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	// Optional error, set only if error happened.
	Error *protos.TError `protobuf:"bytes,1,opt,name=Error,proto3" json:"Error,omitempty"`
	// Optional response headers.
	Headers *TResponseHeaders `protobuf:"bytes,1000,opt,name=Headers,proto3" json:"Headers,omitempty"`
}

func (x *TDestroyCheckpointResponse) Reset() {
	*x = TDestroyCheckpointResponse{}
	if protoimpl.UnsafeEnabled {
		mi := &file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes[3]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TDestroyCheckpointResponse) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TDestroyCheckpointResponse) ProtoMessage() {}

func (x *TDestroyCheckpointResponse) ProtoReflect() protoreflect.Message {
	mi := &file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes[3]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TDestroyCheckpointResponse.ProtoReflect.Descriptor instead.
func (*TDestroyCheckpointResponse) Descriptor() ([]byte, []int) {
	return file_cloud_filestore_public_api_protos_checkpoint_proto_rawDescGZIP(), []int{3}
}

func (x *TDestroyCheckpointResponse) GetError() *protos.TError {
	if x != nil {
		return x.Error
	}
	return nil
}

func (x *TDestroyCheckpointResponse) GetHeaders() *TResponseHeaders {
	if x != nil {
		return x.Headers
	}
	return nil
}

var File_cloud_filestore_public_api_protos_checkpoint_proto protoreflect.FileDescriptor

var file_cloud_filestore_public_api_protos_checkpoint_proto_rawDesc = []byte{
	0x0a, 0x32, 0x63, 0x6c, 0x6f, 0x75, 0x64, 0x2f, 0x66, 0x69, 0x6c, 0x65, 0x73, 0x74, 0x6f, 0x72,
	0x65, 0x2f, 0x70, 0x75, 0x62, 0x6c, 0x69, 0x63, 0x2f, 0x61, 0x70, 0x69, 0x2f, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x73, 0x2f, 0x63, 0x68, 0x65, 0x63, 0x6b, 0x70, 0x6f, 0x69, 0x6e, 0x74, 0x2e, 0x70,
	0x72, 0x6f, 0x74, 0x6f, 0x12, 0x18, 0x4e, 0x43, 0x6c, 0x6f, 0x75, 0x64, 0x2e, 0x4e, 0x46, 0x69,
	0x6c, 0x65, 0x53, 0x74, 0x6f, 0x72, 0x65, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x2f,
	0x63, 0x6c, 0x6f, 0x75, 0x64, 0x2f, 0x66, 0x69, 0x6c, 0x65, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x2f,
	0x70, 0x75, 0x62, 0x6c, 0x69, 0x63, 0x2f, 0x61, 0x70, 0x69, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f,
	0x73, 0x2f, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72, 0x73, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a,
	0x25, 0x63, 0x6c, 0x6f, 0x75, 0x64, 0x2f, 0x73, 0x74, 0x6f, 0x72, 0x61, 0x67, 0x65, 0x2f, 0x63,
	0x6f, 0x72, 0x65, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x73, 0x2f, 0x65, 0x72, 0x72, 0x6f, 0x72,
	0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x22, 0xb8, 0x01, 0x0a, 0x18, 0x54, 0x43, 0x72, 0x65, 0x61,
	0x74, 0x65, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x70, 0x6f, 0x69, 0x6e, 0x74, 0x52, 0x65, 0x71, 0x75,
	0x65, 0x73, 0x74, 0x12, 0x3c, 0x0a, 0x07, 0x48, 0x65, 0x61, 0x64, 0x65, 0x72, 0x73, 0x18, 0x01,
	0x20, 0x01, 0x28, 0x0b, 0x32, 0x22, 0x2e, 0x4e, 0x43, 0x6c, 0x6f, 0x75, 0x64, 0x2e, 0x4e, 0x46,
	0x69, 0x6c, 0x65, 0x53, 0x74, 0x6f, 0x72, 0x65, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e,
	0x54, 0x48, 0x65, 0x61, 0x64, 0x65, 0x72, 0x73, 0x52, 0x07, 0x48, 0x65, 0x61, 0x64, 0x65, 0x72,
	0x73, 0x12, 0x22, 0x0a, 0x0c, 0x46, 0x69, 0x6c, 0x65, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x49,
	0x64, 0x18, 0x02, 0x20, 0x01, 0x28, 0x09, 0x52, 0x0c, 0x46, 0x69, 0x6c, 0x65, 0x53, 0x79, 0x73,
	0x74, 0x65, 0x6d, 0x49, 0x64, 0x12, 0x22, 0x0a, 0x0c, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x70, 0x6f,
	0x69, 0x6e, 0x74, 0x49, 0x64, 0x18, 0x03, 0x20, 0x01, 0x28, 0x09, 0x52, 0x0c, 0x43, 0x68, 0x65,
	0x63, 0x6b, 0x70, 0x6f, 0x69, 0x6e, 0x74, 0x49, 0x64, 0x12, 0x16, 0x0a, 0x06, 0x4e, 0x6f, 0x64,
	0x65, 0x49, 0x64, 0x18, 0x04, 0x20, 0x01, 0x28, 0x04, 0x52, 0x06, 0x4e, 0x6f, 0x64, 0x65, 0x49,
	0x64, 0x22, 0x8f, 0x01, 0x0a, 0x19, 0x54, 0x43, 0x72, 0x65, 0x61, 0x74, 0x65, 0x43, 0x68, 0x65,
	0x63, 0x6b, 0x70, 0x6f, 0x69, 0x6e, 0x74, 0x52, 0x65, 0x73, 0x70, 0x6f, 0x6e, 0x73, 0x65, 0x12,
	0x2b, 0x0a, 0x05, 0x45, 0x72, 0x72, 0x6f, 0x72, 0x18, 0x01, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x15,
	0x2e, 0x4e, 0x43, 0x6c, 0x6f, 0x75, 0x64, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54,
	0x45, 0x72, 0x72, 0x6f, 0x72, 0x52, 0x05, 0x45, 0x72, 0x72, 0x6f, 0x72, 0x12, 0x45, 0x0a, 0x07,
	0x48, 0x65, 0x61, 0x64, 0x65, 0x72, 0x73, 0x18, 0xe8, 0x07, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x2a,
	0x2e, 0x4e, 0x43, 0x6c, 0x6f, 0x75, 0x64, 0x2e, 0x4e, 0x46, 0x69, 0x6c, 0x65, 0x53, 0x74, 0x6f,
	0x72, 0x65, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x52, 0x65, 0x73, 0x70, 0x6f,
	0x6e, 0x73, 0x65, 0x48, 0x65, 0x61, 0x64, 0x65, 0x72, 0x73, 0x52, 0x07, 0x48, 0x65, 0x61, 0x64,
	0x65, 0x72, 0x73, 0x22, 0xa1, 0x01, 0x0a, 0x19, 0x54, 0x44, 0x65, 0x73, 0x74, 0x72, 0x6f, 0x79,
	0x43, 0x68, 0x65, 0x63, 0x6b, 0x70, 0x6f, 0x69, 0x6e, 0x74, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73,
	0x74, 0x12, 0x3c, 0x0a, 0x07, 0x48, 0x65, 0x61, 0x64, 0x65, 0x72, 0x73, 0x18, 0x01, 0x20, 0x01,
	0x28, 0x0b, 0x32, 0x22, 0x2e, 0x4e, 0x43, 0x6c, 0x6f, 0x75, 0x64, 0x2e, 0x4e, 0x46, 0x69, 0x6c,
	0x65, 0x53, 0x74, 0x6f, 0x72, 0x65, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x48,
	0x65, 0x61, 0x64, 0x65, 0x72, 0x73, 0x52, 0x07, 0x48, 0x65, 0x61, 0x64, 0x65, 0x72, 0x73, 0x12,
	0x22, 0x0a, 0x0c, 0x46, 0x69, 0x6c, 0x65, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x49, 0x64, 0x18,
	0x02, 0x20, 0x01, 0x28, 0x09, 0x52, 0x0c, 0x46, 0x69, 0x6c, 0x65, 0x53, 0x79, 0x73, 0x74, 0x65,
	0x6d, 0x49, 0x64, 0x12, 0x22, 0x0a, 0x0c, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x70, 0x6f, 0x69, 0x6e,
	0x74, 0x49, 0x64, 0x18, 0x03, 0x20, 0x01, 0x28, 0x09, 0x52, 0x0c, 0x43, 0x68, 0x65, 0x63, 0x6b,
	0x70, 0x6f, 0x69, 0x6e, 0x74, 0x49, 0x64, 0x22, 0x90, 0x01, 0x0a, 0x1a, 0x54, 0x44, 0x65, 0x73,
	0x74, 0x72, 0x6f, 0x79, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x70, 0x6f, 0x69, 0x6e, 0x74, 0x52, 0x65,
	0x73, 0x70, 0x6f, 0x6e, 0x73, 0x65, 0x12, 0x2b, 0x0a, 0x05, 0x45, 0x72, 0x72, 0x6f, 0x72, 0x18,
	0x01, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x15, 0x2e, 0x4e, 0x43, 0x6c, 0x6f, 0x75, 0x64, 0x2e, 0x4e,
	0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x45, 0x72, 0x72, 0x6f, 0x72, 0x52, 0x05, 0x45, 0x72,
	0x72, 0x6f, 0x72, 0x12, 0x45, 0x0a, 0x07, 0x48, 0x65, 0x61, 0x64, 0x65, 0x72, 0x73, 0x18, 0xe8,
	0x07, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x2a, 0x2e, 0x4e, 0x43, 0x6c, 0x6f, 0x75, 0x64, 0x2e, 0x4e,
	0x46, 0x69, 0x6c, 0x65, 0x53, 0x74, 0x6f, 0x72, 0x65, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f,
	0x2e, 0x54, 0x52, 0x65, 0x73, 0x70, 0x6f, 0x6e, 0x73, 0x65, 0x48, 0x65, 0x61, 0x64, 0x65, 0x72,
	0x73, 0x52, 0x07, 0x48, 0x65, 0x61, 0x64, 0x65, 0x72, 0x73, 0x42, 0x34, 0x5a, 0x32, 0x61, 0x2e,
	0x79, 0x61, 0x6e, 0x64, 0x65, 0x78, 0x2d, 0x74, 0x65, 0x61, 0x6d, 0x2e, 0x72, 0x75, 0x2f, 0x63,
	0x6c, 0x6f, 0x75, 0x64, 0x2f, 0x66, 0x69, 0x6c, 0x65, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x2f, 0x70,
	0x75, 0x62, 0x6c, 0x69, 0x63, 0x2f, 0x61, 0x70, 0x69, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x73,
	0x62, 0x06, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x33,
}

var (
	file_cloud_filestore_public_api_protos_checkpoint_proto_rawDescOnce sync.Once
	file_cloud_filestore_public_api_protos_checkpoint_proto_rawDescData = file_cloud_filestore_public_api_protos_checkpoint_proto_rawDesc
)

func file_cloud_filestore_public_api_protos_checkpoint_proto_rawDescGZIP() []byte {
	file_cloud_filestore_public_api_protos_checkpoint_proto_rawDescOnce.Do(func() {
		file_cloud_filestore_public_api_protos_checkpoint_proto_rawDescData = protoimpl.X.CompressGZIP(file_cloud_filestore_public_api_protos_checkpoint_proto_rawDescData)
	})
	return file_cloud_filestore_public_api_protos_checkpoint_proto_rawDescData
}

var file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes = make([]protoimpl.MessageInfo, 4)
var file_cloud_filestore_public_api_protos_checkpoint_proto_goTypes = []interface{}{
	(*TCreateCheckpointRequest)(nil),   // 0: NCloud.NFileStore.NProto.TCreateCheckpointRequest
	(*TCreateCheckpointResponse)(nil),  // 1: NCloud.NFileStore.NProto.TCreateCheckpointResponse
	(*TDestroyCheckpointRequest)(nil),  // 2: NCloud.NFileStore.NProto.TDestroyCheckpointRequest
	(*TDestroyCheckpointResponse)(nil), // 3: NCloud.NFileStore.NProto.TDestroyCheckpointResponse
	(*THeaders)(nil),                   // 4: NCloud.NFileStore.NProto.THeaders
	(*protos.TError)(nil),              // 5: NCloud.NProto.TError
	(*TResponseHeaders)(nil),           // 6: NCloud.NFileStore.NProto.TResponseHeaders
}
var file_cloud_filestore_public_api_protos_checkpoint_proto_depIdxs = []int32{
	4, // 0: NCloud.NFileStore.NProto.TCreateCheckpointRequest.Headers:type_name -> NCloud.NFileStore.NProto.THeaders
	5, // 1: NCloud.NFileStore.NProto.TCreateCheckpointResponse.Error:type_name -> NCloud.NProto.TError
	6, // 2: NCloud.NFileStore.NProto.TCreateCheckpointResponse.Headers:type_name -> NCloud.NFileStore.NProto.TResponseHeaders
	4, // 3: NCloud.NFileStore.NProto.TDestroyCheckpointRequest.Headers:type_name -> NCloud.NFileStore.NProto.THeaders
	5, // 4: NCloud.NFileStore.NProto.TDestroyCheckpointResponse.Error:type_name -> NCloud.NProto.TError
	6, // 5: NCloud.NFileStore.NProto.TDestroyCheckpointResponse.Headers:type_name -> NCloud.NFileStore.NProto.TResponseHeaders
	6, // [6:6] is the sub-list for method output_type
	6, // [6:6] is the sub-list for method input_type
	6, // [6:6] is the sub-list for extension type_name
	6, // [6:6] is the sub-list for extension extendee
	0, // [0:6] is the sub-list for field type_name
}

func init() { file_cloud_filestore_public_api_protos_checkpoint_proto_init() }
func file_cloud_filestore_public_api_protos_checkpoint_proto_init() {
	if File_cloud_filestore_public_api_protos_checkpoint_proto != nil {
		return
	}
	file_cloud_filestore_public_api_protos_headers_proto_init()
	if !protoimpl.UnsafeEnabled {
		file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TCreateCheckpointRequest); i {
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
		file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TCreateCheckpointResponse); i {
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
		file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes[2].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TDestroyCheckpointRequest); i {
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
		file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes[3].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TDestroyCheckpointResponse); i {
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
			RawDescriptor: file_cloud_filestore_public_api_protos_checkpoint_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   4,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_cloud_filestore_public_api_protos_checkpoint_proto_goTypes,
		DependencyIndexes: file_cloud_filestore_public_api_protos_checkpoint_proto_depIdxs,
		MessageInfos:      file_cloud_filestore_public_api_protos_checkpoint_proto_msgTypes,
	}.Build()
	File_cloud_filestore_public_api_protos_checkpoint_proto = out.File
	file_cloud_filestore_public_api_protos_checkpoint_proto_rawDesc = nil
	file_cloud_filestore_public_api_protos_checkpoint_proto_goTypes = nil
	file_cloud_filestore_public_api_protos_checkpoint_proto_depIdxs = nil
}
